#include "Texture.h"
#include <iostream>
#include "lodepng\lodepng.h"
#include "ProgramUtilities.h"
#include <stdexcept>

Texture::Texture(Renderer& renderer, TextureType type, const std::string& filename, bool gammaSpace) : renderer(renderer) {
	switch (type) {
	case _Image:
		Init(filename, gammaSpace);
		break;
	case Cubemap:
		InitCubemap(filename, gammaSpace);
		break;
	default:
		throw std::runtime_error("Unsupported");
	}
}

Texture::Texture(Renderer& renderer, TextureType type, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkFormat format) : renderer(renderer) {
	switch (type) {
	case _Image:
		Init(width, height, format, usage);
		break;
	case Depth:
		InitDepth(width, height, usage);
		break;
	default:
		throw std::runtime_error("Unsupported");
	}
}

Texture::~Texture() {
	renderer.memory->Free(image.alloc);
	vkDestroyImage(renderer.device, image.image, nullptr);
	vkDestroyImageView(renderer.device, imageView, nullptr);
}

void Texture::Init(const std::string& filename, bool gammaSpace) {
	LoadImages(std::vector<std::string>{ filename });
	CalulateMipChain();

	if (gammaSpace) {
		format = VK_FORMAT_R8G8B8A8_SRGB;
	}
	else {
		format = VK_FORMAT_R8G8B8A8_UNORM;
	}

	image = CreateImage(renderer,
		format,
		width, height,
		mipLevels, arrayLayers,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		0);
	imageView = CreateImageView(renderer.device, image.image, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, mipLevels, arrayLayers);
}

void Texture::InitCubemap(const std::string& filenameRoot, bool gammaSpace) {
	//to create a cubemap, there must 6 layers in an image
	//the layers correspond to +X, -X, +Y, -Y, +Z, -Z
	//Vulkan uses Y-down convention, so +Y corresponds to down
	std::vector<std::string> filenames = {
		filenameRoot + "_r.png",
		filenameRoot + "_l.png",
		filenameRoot + "_d.png",
		filenameRoot + "_u.png",
		filenameRoot + "_b.png",
		filenameRoot + "_f.png",
	};

	LoadImages(filenames);
	CalulateMipChain();

	if (gammaSpace) {
		format = VK_FORMAT_R8G8B8A8_SRGB;
	} else {
		format = VK_FORMAT_R8G8B8A8_UNORM;
	}

	image = CreateImage(renderer,
		format,
		width, height,
		mipLevels, arrayLayers,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
	imageView = CreateImageView(renderer.device, image.image, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, mipLevels, arrayLayers);
}

void Texture::Init(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage) {
	mipLevels = 1;
	arrayLayers = 1;
	this->format = format;
	this->width = width;
	this->height = height;

	image = CreateImage(renderer,
		format,
		width, height,
		mipLevels, arrayLayers,
		usage, 0);
	imageView = CreateImageView(renderer.device, image.image, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, mipLevels, arrayLayers);
}

void Texture::InitDepth(uint32_t width, uint32_t height, VkImageUsageFlags flags) {
	this->width = width;
	this->height = height;

	format = findDepthFormat();
	image = CreateImage(renderer,
		format, width, height,
		1, 1,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | flags, 0);
	imageView = CreateImageView(renderer.device, image.image,
		format, VK_IMAGE_ASPECT_DEPTH_BIT,
		VK_IMAGE_VIEW_TYPE_2D, 1, 1);
}

void Texture::LoadImages(std::vector<std::string>& filenames) {
	data.resize(filenames.size());
	unsigned int width, height;

	for (size_t i = 0; i < filenames.size(); i++) {
		std::cout << "Loading: " << filenames[i] << std::endl;
		unsigned int error = lodepng::decode(data[i], width, height, filenames[i]);
		if (error != 0) {
			std::cerr << "Unable to load the texture at path " << filenames[i] << std::endl;
		}
		flipImage(data[i], width, height);
	}

	this->width = static_cast<uint32_t>(width);
	this->height = static_cast<uint32_t>(height);
	arrayLayers = static_cast<uint32_t>(data.size());
}

uint32_t Texture::GetWidth() {
	return width;
}

uint32_t Texture::GetHeight() {
	return height;
}

void Texture::UploadData(VkCommandBuffer commandBuffer, std::vector<std::unique_ptr<StagingBuffer>>& stagingBuffers) {
	Transition(commandBuffer, VK_FORMAT_R8G8B8A8_UNORM, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, mipLevels, arrayLayers);

	for (size_t i = 0; i < data.size(); i++) {
		stagingBuffers.emplace_back(std::make_unique<StagingBuffer>(renderer, data[i].size(), data[i].data()));
		stagingBuffers.back()->CopyToImage(commandBuffer, image.image, width, height, static_cast<uint32_t>(i));
	}

	GenerateMipChain(commandBuffer);

	Transition(commandBuffer, VK_FORMAT_R8G8B8A8_UNORM, image.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, arrayLayers);
}

void Texture::CalulateMipChain() {
	uint32_t w = width;
	uint32_t h = height;

	while (w != 1 && h != 1) {
		mipChain.push_back({ w, h });
		if (w > 1) w /= 2;
		if (h > 1) h /= 2;
	}

	mipLevels = static_cast<uint32_t>(mipChain.size());
}

void Texture::GenerateMipChain(VkCommandBuffer commandBuffer) {
	if (mipChain.size() == 1) return;

	//start from i == 1, blit level (i - 1) to (i)
	for (size_t i = 1; i < mipChain.size(); i++) {
		glm::vec2 src = mipChain[i - 1];
		glm::vec2 dst = mipChain[i];
		int32_t srcW = static_cast<int32_t>(src.x);
		int32_t srcH = static_cast<int32_t>(src.y);
		int32_t dstW = static_cast<int32_t>(dst.x);
		int32_t dstH = static_cast<int32_t>(dst.y);

		VkImageBlit blit = {};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = static_cast<uint32_t>(data.size());
		blit.srcSubresource.mipLevel = static_cast<uint32_t>(i - 1);
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { srcW, srcH, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = static_cast<uint32_t>(data.size());
		blit.dstSubresource.mipLevel = static_cast<uint32_t>(i);
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { dstW, dstH, 1 };

		vkCmdBlitImage(commandBuffer,
			image.image, VK_IMAGE_LAYOUT_GENERAL,
			image.image, VK_IMAGE_LAYOUT_GENERAL,
			1, &blit,
			VK_FILTER_LINEAR
		);

		Transition(commandBuffer, format, image.image,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			mipLevels, arrayLayers);
	}
}

VkFormat Texture::findSupportedFormat(const std::vector<VkFormat>& candidates, VkFormatFeatureFlags features) {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(renderer.physicalDevice, format, &props);

		if ((props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("Could not find supported format");
}

VkFormat Texture::findDepthFormat() {
	return findSupportedFormat(
	{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}