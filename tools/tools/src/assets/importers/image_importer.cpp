#include "image_importer.h"
#include "halley/tools/assets/import_assets_database.h"
#include "halley/file/byte_serializer.h"
#include "halley/resources/metadata.h"
#include "halley/file_formats/image.h"
#include "halley/tools/file/filesystem.h"
#include "halley/maths/colour.h"

using namespace Halley;

void ImageImporter::import(const ImportingAsset& asset, IAssetCollector& collector)
{
	// Prepare metadata
	Metadata meta;
	if (asset.metadata) {
		meta = *asset.metadata;
	}

	// Load image
	Path mainFile = asset.inputFiles.at(0).name;
	auto span = gsl::as_bytes(gsl::span<const Byte>(asset.inputFiles[0].data));
	std::unique_ptr<Image> image;
	if (meta.getString("compression", "png") == "png") {
		image = std::make_unique<Image>(span, fromString<Image::Mode>(meta.getString("mode", "undefined")));
	} else {
		image = std::make_unique<Image>();
		Deserializer s(span);
		s >> *image;
	}

	// Convert to indexed mode
	auto palette = meta.getString("palette", "");
	if (palette != "") {
		auto paletteBytes = collector.readAdditionalFile(palette);
		Image paletteImage(gsl::as_bytes(gsl::span<Byte>(paletteBytes)));
		image = convertToIndexed(*image, paletteImage);
	}

	// Fill meta
	Vector2i size = image->getSize();
	meta.set("width", size.x);
	meta.set("height", size.y);
	meta.set("mode", toString(image->getMode()));

	// Output
	ImportingAsset imageAsset;
	imageAsset.assetId = asset.assetId;
	imageAsset.assetType = ImportAssetType::Texture;
	imageAsset.metadata = std::make_unique<Metadata>(meta);
	imageAsset.inputFiles.emplace_back(ImportingAssetFile(asset.assetId, Serializer::toBytes(*image)));
	collector.addAdditionalAsset(std::move(imageAsset));
}

std::unique_ptr<Image> ImageImporter::convertToIndexed(const Image& image, const Image& palette)
{
	auto lookup = makePaletteConversion(palette);

	auto result = std::make_unique<Image>(Image::Mode::Indexed, image.getSize());
	auto dst = result->getPixels();
	auto src = reinterpret_cast<const int*>(image.getPixels());
	size_t n = image.getWidth() * image.getHeight();

	for (size_t i = 0; i < n; ++i) {
		auto res = lookup.find(src[i]);
		if (res == lookup.end()) {
			unsigned int r, g, b, a;
			Image::convertIntToRGBA(src[i], r, g, b, a);
			throw Exception("Colour " + Colour(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f).toString() + " doesn't exist in palette.");
		}
		dst[i] = res->second;
	}

	return result;
}

std::unordered_map<int, int> ImageImporter::makePaletteConversion(const Image& palette)
{
	auto src = reinterpret_cast<const int*>(palette.getPixels());
	size_t w = palette.getWidth();
	size_t h = palette.getHeight();

	std::unordered_map<int, int> lookup;
	for (size_t y = 0; y < h; ++y) {
		for (size_t x = 0; x < w; ++x) {
			auto col = src[y * w + x];

			auto res = lookup.find(col);
			if (res == lookup.end()) {
				lookup[col] = int(x);
			} else if (col != 0) {
				unsigned int r, g, b, a;
				Image::convertIntToRGBA(col, r, g, b, a);
				throw Exception("Colour " + Colour(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f).toString()
					+ " is duplicated in the palette. Found at " + toString(x) + ", " + toString(y) + "; previously found at index "
					+ toString(res->second));
			}
		}
	}
	return lookup;
}
