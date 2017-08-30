#pragma once
#include "Renderer.h"
#include "ProgramUtilities.h"
#include <vector>
#include "StagingBuffer.h"

class Skybox {
public:
	Skybox(Renderer& renderr);
	~Skybox();
	void UploadData(VkCommandBuffer commandBuffer, std::vector<std::unique_ptr<StagingBuffer>>& stagingBuffers);
	void Draw(VkCommandBuffer commandBuffer);

	static std::vector<VkVertexInputBindingDescription> GetBindingDescriptions();
	static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();

private:
	Renderer& renderer;

	Buffer vertexBuffer;
	Buffer indexBuffer;

	void Init();
};