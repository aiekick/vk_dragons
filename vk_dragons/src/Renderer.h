#pragma once
#define GLFW_INCLUDE_VULKAN
#include<GLFW/glfw3.h>
#include <vector>
#include <memory>
#include "MemorySystem.h"

struct QueueFamilyIndices {
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete() {
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

class Renderer {
public:
	Renderer(GLFWwindow* window, uint32_t width, uint32_t height);
	~Renderer();

	void Acquire();
	uint32_t GetImageIndex();
	void Render(VkCommandBuffer commandBuffer);
	void Present();

	void Resize(uint32_t width, uint32_t height);
	void ToggleVSync();

	uint32_t GetWidth();
	uint32_t GetHeight();

	bool IsGamma();

	VkCommandBuffer GetSingleUseCommandBuffer();
	void SubmitCommandBuffer(VkCommandBuffer commandBuffer);

	std::unique_ptr<Memory> memory;

	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkDevice device;
	VkExtent2D swapchainExtent;
	VkCommandPool commandPool;
	std::vector<VkImage> swapchainImages;
	VkFormat swapchainImageFormat;
	std::vector<VkImageView> swapchainImageViews;

private:
	GLFWwindow* window;
	uint32_t width;
	uint32_t height;
	bool vsync;
	bool gamma;

	VkInstance instance;
	VkQueue graphicsQueue;
	VkSurfaceKHR surface;
	VkQueue presentQueue;
	VkSwapchainKHR swapchain;

	VkSemaphore imageAvailableSemaphore;
	VkSemaphore renderFinishedSemaphore;
	uint32_t imageIndex;
	std::vector<VkFence> fences;

	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	void createInstance();
	bool checkValidationSupport(const std::vector<const char*>& layers);
	void pickPhysicalDevice();
	bool isDeviceSuitable(VkPhysicalDevice device);
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
	void SelectFeatures(VkPhysicalDeviceFeatures& features);
	void createLogicalDevice();
	void createSurface();
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	SwapChainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
	void createSwapchain();
	void createImageViews();
	void createFences();
	void createSemaphores();
	void createCommandPool();
	void recreateSwapchain();
	void cleanupSwapchain();
};

