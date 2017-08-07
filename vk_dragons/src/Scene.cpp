#include "Scene.h"

Scene::Scene(GLFWwindow* window, uint32_t width, uint32_t height)
	: renderer(window, width, height),
	dragon(renderer),
	suzanne(renderer),
	plane(renderer),
	skybox(renderer),
	dragonColor(renderer),
	suzanneColor(renderer),
	planeColor(renderer),
	skyboxColor(renderer),
	camera(45.0f, width, height),
	input(window, camera),
	lightDepth(renderer),
	depth(renderer) {

	time = 0.0f;
	camera.SetPosition(glm::vec3(0, 0, 1.0f));

	dragon.Init("resources/dragon.obj");
	suzanne.Init("resources/suzanne.obj");
	plane.Init("resources/plane.obj");
	skybox.Init();

	dragon.GetTransform().SetScale(glm::vec3(0.5f));
	dragon.GetTransform().SetPosition(glm::vec3(-0.1f, 0.0f, -0.25f));

	suzanne.GetTransform().SetScale(glm::vec3(0.25f));
	suzanne.GetTransform().SetPosition(glm::vec3(0.2f, 0, 0));

	plane.GetTransform().SetScale(glm::vec3(2.0f));
	plane.GetTransform().SetPosition(glm::vec3(0.0f, -0.35f, -0.5f));

	dragonColor.Init("resources/dragon_texture_color.png");
	suzanneColor.Init("resources/suzanne_texture_color.png");
	planeColor.Init("resources/plane_texture_color.png");
	skyboxColor.InitCubemap("resources/cubemap/cubemap");

	UploadResources();

	lightDepth.Init(512, 512);

	createSwapchainResources(width, height);

	CreateLightRenderPass();
	CreateLightFramebuffer();

	CreateSampler();
	CreateUniformSetLayout();
	CreateTextureSetLayout();
	CreateUniformBuffer();
	CreateDescriptorPool();
	CreateUniformSet();
	CreateTextureSet(dragonColor.imageView, dragonTextureSet);
	CreateTextureSet(suzanneColor.imageView, suzanneTextureSet);
	CreateTextureSet(planeColor.imageView, planeTextureSet);
	CreateTextureSet(skyboxColor.imageView, skyboxTextureSet);

	CreatePipelines();

	AllocateCommandBuffers();
}

Scene::~Scene() {
	vkDeviceWaitIdle(renderer.device);
	lightDepth.Cleanup();
	CleanupSwapchainResources();
	vkDestroyRenderPass(renderer.device, lightRenderPass, nullptr);
	vkDestroyFramebuffer(renderer.device, lightFramebuffer, nullptr);
	vkDestroyDescriptorSetLayout(renderer.device, uniformSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(renderer.device, textureSetLayout, nullptr);
	vkDestroyBuffer(renderer.device, uniformBuffer.buffer, nullptr);
	vkDestroyDescriptorPool(renderer.device, descriptorPool, nullptr);
	vkDestroySampler(renderer.device, sampler, nullptr);
	DestroyPipelines();
}

void Scene::CleanupSwapchainResources() {
	depth.Cleanup();
	for (auto& framebuffer : swapChainFramebuffers) {
		vkDestroyFramebuffer(renderer.device, framebuffer, nullptr);
	}
	vkDestroyRenderPass(renderer.device, mainRenderPass, nullptr);
}

void Scene::UploadResources() {
	VkCommandBuffer commandBuffer = renderer.GetSingleUseCommandBuffer();

	dragon.UploadData(commandBuffer);
	suzanne.UploadData(commandBuffer);
	plane.UploadData(commandBuffer);
	skybox.UploadData(commandBuffer);

	dragonColor.UploadData(commandBuffer);
	suzanneColor.UploadData(commandBuffer);
	planeColor.UploadData(commandBuffer);
	skyboxColor.UploadData(commandBuffer);

	renderer.SubmitCommandBuffer(commandBuffer);

	dragon.DestroyStaging();
	suzanne.DestroyStaging();
	plane.DestroyStaging();
	skybox.DestroyStaging();

	dragonColor.DestroyStaging();
	suzanneColor.DestroyStaging();
	planeColor.DestroyStaging();
	skyboxColor.DestroyStaging();

	renderer.memory->hostAllocator->Reset();
}

void Scene::UpdateUniform() {
	char* ptr = reinterpret_cast<char*>(renderer.memory->hostMapping) + uniformBuffer.offset;
	Uniform* uniform = reinterpret_cast<Uniform*>(ptr);
	uniform->camProjection = camera.GetProjection();
	uniform->camView = camera.GetView();
	uniform->camRotationOnlyView = camera.GetRotationOnlyView();
	uniform->camViewInverse = glm::inverse(camera.GetView());
	uniform->lightProjection = light.GetProjection();
	uniform->lightView = light.GetView();
	uniform->lightIa = light.GetIa();
	uniform->lightId = light.GetId();
	uniform->lightIs = light.GetIs();
	uniform->lightShininess = light.GetShininess();
}

void Scene::Update(double elapsed) {
	time += static_cast<float>(elapsed);
	input.Update(elapsed);
	camera.Update();
	light.SetPosition(glm::vec3(2.0f, (1.5f + sin(0.5*time)), 2.0f));
	UpdateUniform();

	suzanne.GetTransform().SetRotation(time, glm::vec3(0, 1, 0));
}

void Scene::Render() {
	renderer.Acquire();
	uint32_t index = renderer.GetImageIndex();
	RecordCommandBuffer(index);
	renderer.Render(commandBuffers[index]);
	renderer.Present();
}

void Scene::Resize(uint32_t width, uint32_t height) {
	renderer.Resize(width, height);
	camera.SetSize(width, height);
	CleanupSwapchainResources();

	createSwapchainResources(width, height);
	DestroyPipelines();
	CreatePipelines();
	AllocateCommandBuffers();
}

void Scene::createSwapchainResources(uint32_t width, uint32_t height) {
	depth.Init(width, height);
	createRenderPass();
	createFramebuffers();
}

void Scene::AllocateCommandBuffers() {
	if (commandBuffers.size() > 0) vkFreeCommandBuffers(renderer.device, renderer.commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	commandBuffers.clear();
	commandBuffers.resize(swapChainFramebuffers.size());

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = renderer.commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

	if (vkAllocateCommandBuffers(renderer.device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate command buffers!");
	}
}

void Scene::RecordCommandBuffer(uint32_t imageIndex) {
	VkCommandBuffer commandBuffer = commandBuffers[imageIndex];

	vkResetCommandBuffer(commandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	RecordDepthPass(commandBuffer);
	RecordMainPass(commandBuffer, imageIndex);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to record command buffer!");
	}
}

void Scene::RecordDepthPass(VkCommandBuffer commandBuffer) {
	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = lightRenderPass;
	renderPassInfo.framebuffer = lightFramebuffer;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { lightDepth.GetWidth(), lightDepth.GetHeight() };

	VkClearValue clearColor = {};
	clearColor.depthStencil = { 1.0f, 0 };

	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lightPipelineLayout, 0, 1, &uniformSet, 0, nullptr);

	dragon.DrawDepth(commandBuffer, lightPipelineLayout);
	suzanne.DrawDepth(commandBuffer, lightPipelineLayout);

	vkCmdEndRenderPass(commandBuffer);
}

void Scene::RecordMainPass(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = mainRenderPass;
	renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = renderer.swapChainExtent;

	VkClearValue clearColors[2];
	clearColors[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
	clearColors[1].depthStencil = { 1.0f, 0 };
	renderPassInfo.clearValueCount = 2;
	renderPassInfo.pClearValues = clearColors;

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout, 0, 1, &uniformSet, 0, nullptr);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout, 1, 1, &dragonTextureSet, 0, nullptr);
	dragon.Draw(commandBuffer, modelPipelineLayout, camera);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout, 1, 1, &suzanneTextureSet, 0, nullptr);
	suzanne.Draw(commandBuffer, modelPipelineLayout, camera);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, modelPipelineLayout, 1, 1, &planeTextureSet, 0, nullptr);
	plane.Draw(commandBuffer, modelPipelineLayout, camera);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1, &uniformSet, 0, nullptr);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 1, 1, &skyboxTextureSet, 0, nullptr);
	skybox.Draw(commandBuffer);

	vkCmdEndRenderPass(commandBuffer);
}

void Scene::createRenderPass() {
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = renderer.swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depth.format;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(renderer.device, &renderPassInfo, nullptr, &mainRenderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create render pass!");
	}
}

void Scene::createFramebuffers() {
	swapChainFramebuffers.resize(renderer.swapChainImageViews.size());

	for (size_t i = 0; i < renderer.swapChainImageViews.size(); i++) {
		VkImageView attachments[] = {
			renderer.swapChainImageViews[i],
			depth.imageView
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = mainRenderPass;
		framebufferInfo.attachmentCount = 2;
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = renderer.swapChainExtent.width;
		framebufferInfo.height = renderer.swapChainExtent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(renderer.device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create framebuffer!");
		}
	}
}

void Scene::CreateLightRenderPass() {
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = depth.format;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 0;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &depthAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(renderer.device, &renderPassInfo, nullptr, &lightRenderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create render pass!");
	}
}

void Scene::CreateLightFramebuffer() {
	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = lightRenderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &lightDepth.imageView;
	framebufferInfo.width = lightDepth.GetWidth();
	framebufferInfo.height = lightDepth.GetHeight();
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(renderer.device, &framebufferInfo, nullptr, &lightFramebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create framebuffer!");
	}
}

void Scene::CreateSampler() {
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 4.0f;

	if (vkCreateSampler(renderer.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture sampler!");
	}
}

void Scene::CreateUniformSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding = {};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &uboLayoutBinding;

	if (vkCreateDescriptorSetLayout(renderer.device, &layoutInfo, nullptr, &uniformSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create uniform set layout!");
	}
}

void Scene::CreateTextureSetLayout() {
	VkDescriptorSetLayoutBinding textureLayoutBinding = {};
	textureLayoutBinding.binding = 0;
	textureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureLayoutBinding.descriptorCount = 1;
	textureLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &textureLayoutBinding;

	if (vkCreateDescriptorSetLayout(renderer.device, &layoutInfo, nullptr, &textureSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture set layout!");
	}
}

void Scene::CreateUniformBuffer() {
	VkDeviceSize size = sizeof(Uniform);
	uniformBuffer = CreateHostBuffer(renderer, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

void Scene::CreateDescriptorPool() {
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = 5;

	if (vkCreateDescriptorPool(renderer.device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor pool!");
	}
}

void Scene::CreateUniformSet() {
	VkDescriptorSetLayout layouts[] = { uniformSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(renderer.device, &allocInfo, &uniformSet) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate uniform set!");
	}

	VkDescriptorBufferInfo bufferInfo = {};
	bufferInfo.buffer = uniformBuffer.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(Uniform);

	VkWriteDescriptorSet descriptorWrite;

	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = uniformSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &bufferInfo;

	vkUpdateDescriptorSets(renderer.device, 1, &descriptorWrite, 0, nullptr);
}

void Scene::CreateTextureSet(VkImageView imageView, VkDescriptorSet& descriptorSet) {
	VkDescriptorSetLayout layouts[] = { textureSetLayout };
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = layouts;

	if (vkAllocateDescriptorSets(renderer.device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate texture set!");
	}

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = imageView;
	imageInfo.sampler = sampler;

	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(renderer.device, 1, &descriptorWrite, 0, nullptr);
}