#include "Texture.h"
#include <iostream>
#include "lodepng\lodepng.h"
#include "ProgramUtilities.h"
#include <stdexcept>

Texture::Texture(Renderer& renderer) : renderer(renderer) {

}

Texture::~Texture() {
	vkDestroyImage(renderer.device, image, nullptr);
	vkDestroyImageView(renderer.device, imageView, nullptr);
}

void Texture::DestroyStaging() {
	for (auto& stagingBuffer : stagingBuffers) {
		vkDestroyBuffer(renderer.device, stagingBuffer.buffer, nullptr);
	}
}

void Texture::Init(const std::string& filename) {
	isCubemap = false;
	LoadImages(std::vector<std::string>{ filename });
	CalulateMipChain();
	CreateImage();
	CreateImageView();
}

void Texture::InitCubemap(const std::string& filenameRoot) {
	std::vector<std::string> filenames = {
		filenameRoot + "_r.png",
		filenameRoot + "_l.png",
		filenameRoot + "_u.png",
		filenameRoot + "_d.png",
		filenameRoot + "_b.png",
		filenameRoot + "_f.png",
	};
	isCubemap = true;

	LoadImages(filenames);
	CalulateMipChain();
	CreateImage();
	CreateImageView();
}

void Texture::LoadImages(std::vector<std::string>& filenames) {
	data.resize(filenames.size());
	unsigned int width, height;

	for (size_t i = 0; i < filenames.size(); i++) {
		unsigned int error = lodepng::decode(data[i], width, height, filenames[i]);
		if (error != 0) {
			std::cerr << "Unable to load the texture at path " << filenames[i] << std::endl;
		}
		flipImage(data[i], width, height);
	}

	this->width = static_cast<uint32_t>(width);
	this->height = static_cast<uint32_t>(height);
}


void Texture::CreateImage() {
	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = static_cast<uint32_t>(mipChain.size());
	info.arrayLayers = static_cast<uint32_t>(data.size());
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	if (isCubemap) {
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	if (vkCreateImage(renderer.device, &info, nullptr, &image) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(renderer.device, image, &memRequirements);

	Allocation alloc = renderer.memory->deviceAllocator->Alloc(memRequirements.size, memRequirements.alignment);

	vkBindImageMemory(renderer.device, image, alloc.memory, alloc.offset);
}

void Texture::CreateImageView() {
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = image;
	if (isCubemap) {
		info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	}
	else {
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	}
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.components.r = VK_COMPONENT_SWIZZLE_R;
	info.components.g = VK_COMPONENT_SWIZZLE_G;
	info.components.b = VK_COMPONENT_SWIZZLE_B;
	info.components.a = VK_COMPONENT_SWIZZLE_A;
	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = static_cast<uint32_t>(data.size());
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = static_cast<uint32_t>(mipChain.size());

	if (vkCreateImageView(renderer.device, &info, nullptr, &imageView) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create image view!");
	}
}

void Texture::UploadData(VkCommandBuffer commandBuffer) {
	Transition(commandBuffer, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_GENERAL);

	stagingBuffers.resize(data.size());
	for (size_t i = 0; i < data.size(); i++) {
		stagingBuffers[i] = CreateBuffer(renderer.device, data[i].size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, *renderer.memory->hostAllocator);
		char* dest = static_cast<char*>(renderer.memory->hostMapping) + stagingBuffers[i].offset;
		memcpy(dest, data[i].data(), data[i].size());

		VkBufferImageCopy copy = {};
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset = { 0, 0, 0 };
		copy.imageExtent = {
			width,
			height,
			1
		};

		vkCmdCopyBufferToImage(commandBuffer, stagingBuffers[i].buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	}

	GenerateMipChain(commandBuffer);

	Transition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Texture::CalulateMipChain() {
	uint32_t w = width;
	uint32_t h = height;

	while (w != 1 && h != 1) {
		mipChain.push_back({ w, h });
		if (w > 1) w /= 2;
		if (h > 1) h /= 2;
	}
}

void Texture::Transition(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout) {
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = static_cast<uint32_t>(mipChain.size());
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = static_cast<uint32_t>(data.size());

	if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || newLayout == VK_IMAGE_LAYOUT_GENERAL)) {
		barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	else if ((oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || oldLayout == VK_IMAGE_LAYOUT_GENERAL) && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}
	else {
		throw std::invalid_argument("Unsupported layout transition!");
	}

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

void Texture::GenerateMipChain(VkCommandBuffer commandBuffer) {
	if (mipChain.size() == 1) return;

	//start from 1, blit level (n - 1) to (n)
	for (size_t i = 1; i < mipChain.size(); i++) {
		for (size_t j = 0; j < data.size(); j++) {
			glm::vec2 src = mipChain[i - 1];
			glm::vec2 dst = mipChain[i];
			int32_t srcW = static_cast<int32_t>(src.x);
			int32_t srcH = static_cast<int32_t>(src.y);
			int32_t dstW = static_cast<int32_t>(dst.x);
			int32_t dstH = static_cast<int32_t>(dst.y);

			VkImageBlit blit = {};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.baseArrayLayer = static_cast<uint32_t>(j);;
			blit.srcSubresource.layerCount = 1;
			blit.srcSubresource.mipLevel = static_cast<uint32_t>(i - 1);
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { srcW, srcH, 0 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = static_cast<uint32_t>(j);
			blit.dstSubresource.layerCount = 1;
			blit.dstSubresource.mipLevel = static_cast<uint32_t>(i);
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { dstW, dstH, 0 };

			vkCmdBlitImage(commandBuffer,
				image, VK_IMAGE_LAYOUT_GENERAL,
				image, VK_IMAGE_LAYOUT_GENERAL,
				1, &blit,
				VK_FILTER_LINEAR
			);
		}
	}
}
