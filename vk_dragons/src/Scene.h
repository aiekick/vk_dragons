#pragma once
#include "Renderer.h"
#include "Model.h"
#include "Texture.h"
#include "Camera.h"
#include "Input.h"
#include "DepthBuffer.h"
#include "Skybox.h"
#include "Light.h"

struct Uniform {
	glm::mat4 camProjection;
	glm::mat4 camView;
	glm::mat4 camRotationOnlyView;
	glm::mat4 camViewInverse;
	glm::mat4 lightProjection;
	glm::mat4 lightView;
	glm::vec4 lightPosition;
	glm::vec4 lightIa;
	glm::vec4 lightId;
	glm::vec4 lightIs;
	float lightShininess;
};

class Scene {
public:
	Scene(GLFWwindow* window, uint32_t width, uint32_t height);
	~Scene();

	void Update(double elapsed);
	void Render();
	void Resize(uint32_t width, uint32_t height);

private:
	Renderer renderer;
	Camera camera;
	Input input;
	float time;

	Light light;

	Model dragon;
	Model suzanne;
	Model plane;
	Skybox skybox;
	Texture dragonColor;
	Texture suzanneColor;
	Texture planeColor;
	Texture skyboxColor;

	DepthBuffer lightDepth;
	DepthBuffer depth;

	VkRenderPass mainRenderPass;
	std::vector<VkFramebuffer> swapChainFramebuffers;
	std::vector<VkCommandBuffer> commandBuffers;
	VkSampler sampler;
	VkDescriptorSetLayout uniformSetLayout;
	VkDescriptorSetLayout textureSetLayout;
	VkDescriptorSetLayout skyboxSetLayout;
	Buffer uniformBuffer;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet uniformSet;
	VkDescriptorSet dragonTextureSet;
	VkDescriptorSet suzanneTextureSet;
	VkDescriptorSet planeTextureSet;
	VkDescriptorSet skyboxTextureSet;

	VkRenderPass lightRenderPass;
	VkFramebuffer lightFramebuffer;

	void UploadResources();
	void UpdateUniform();

	void createRenderPass();
	void createFramebuffers();
	void CreateLightRenderPass();
	void CreateLightFramebuffer();
	void AllocateCommandBuffers();
	void RecordCommandBuffer(uint32_t imageIndex);
	void RecordDepthPass(VkCommandBuffer commandBuffer);
	void RecordMainPass(VkCommandBuffer commandBuffer, uint32_t imageIndex);
	void CreateSampler();
	void CreateUniformSetLayout();
	void CreateTextureSetLayout();
	void CreateSkyboxSetLayout();
	void CreateUniformBuffer();
	void CreateDescriptorPool();
	void CreateUniformSet();
	void CreateTextureSet(VkImageView imageView, VkDescriptorSet& descriptorSet);
	void CreateSkyboxSet();

	void createSwapchainResources(uint32_t width, uint32_t height);
	void CleanupSwapchainResources();

	//defined in Scene_pipelines.cpp
	VkPipelineLayout modelPipelineLayout;
	VkPipelineLayout skyboxPipelineLayout;
	VkPipelineLayout lightPipelineLayout;
	VkPipeline modelPipeline;
	VkPipeline planePipeline;
	VkPipeline skyboxPipeline;
	VkPipeline lightPipeline;
	void CreatePipelines();
	void DestroyPipelines();
	void CreateModelPipelineLayout();
	void CreateModelPipeline();
	void CreatePlanePipeline();
	void CreateSkyboxPipelineLayout();
	void CreateSkyboxPipeline();
	void CreateLightPipelineLayout();
	void CreateLightPipeline();
};

