#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>
#include <vulkan/vulkan.h>

const uint32_t WIDTH = 1280;
const uint32_t HEIGHT = 720;
const int MAX_FRAMES_IN_FLIGHT = 2;

// Thread Pool for multi-threaded tasks
class ThreadPool {
public:
  ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i)
      workers.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }
          task();
        }
      });
  }

  template <class F> void enqueue(F &&f) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      tasks.emplace(std::forward<F>(f));
    }
    condition.notify_one();
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
  }

private:
  std::vector<std::jthread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

// Application
class BlackHoleRenderer {
public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  GLFWwindow *window;
  VkInstance instance;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device;
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  VkFormat swapChainImageFormat;
  VkExtent2D swapChainExtent;
  std::vector<VkImageView> swapChainImageViews;
  VkPipelineLayout pipelineLayout;
  VkRenderPass renderPass;
  VkPipeline graphicsPipeline;
  std::vector<VkFramebuffer> swapChainFramebuffers;
  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;
  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame = 0;

  double lastMouseX = 0, lastMouseY = 0;
  float cameraYaw = 0.0f;
  float cameraPitch = 0.2f;
  float cameraRadius = 10.0f;
  float spin_a = 0.99f;
  bool mouseDragging = false;

  // SPH Particle System
  static const uint32_t PARTICLE_COUNT = 65536; // 64K particles
  VkBuffer particleBuffer;
  VkDeviceMemory particleBufferMemory;
  VkShaderModule sphShaderModule;
  VkPipeline sphComputePipeline;
  VkPipelineLayout sphPipelineLayout;
  VkCommandPool sphCommandPool;
  VkCommandBuffer sphCommandBuffer;
  VkDescriptorSet sphDescriptorSet;
  VkDescriptorPool sphDescriptorPool;
  VkDescriptorSet graphicsParticleDescriptorSet;
  VkDescriptorPool graphicsParticleDescriptorPool;
  float simTime = 0.0f;

  struct SPHParams {
    float dt;
    float time;
    float spin_a;
    float mass;
    float viscosity;
    float alpha;
    float beta;
    uint32_t particleCount;
    float innerRadius;
    float outerRadius;
  };

  struct PushConstants {
    float params[4];
    float cameraPos[4];
  };

  void initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window =
        glfwCreateWindow(WIDTH, HEIGHT, "OpenKerr", nullptr, nullptr);

    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y) {
      auto app =
          reinterpret_cast<BlackHoleRenderer *>(glfwGetWindowUserPointer(w));
      app->onMouseMoved(x, y);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int button, int action,
                                          int mods) {
      auto app =
          reinterpret_cast<BlackHoleRenderer *>(glfwGetWindowUserPointer(w));
      app->onMouseButton(button, action, mods);
    });
    glfwSetScrollCallback(window, [](GLFWwindow *w, double xoffset,
                                     double yoffset) {
      auto app =
          reinterpret_cast<BlackHoleRenderer *>(glfwGetWindowUserPointer(w));
      app->onScroll(xoffset, yoffset);
    });
    glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int scancode,
                                  int action, int mods) {
      auto app =
          reinterpret_cast<BlackHoleRenderer *>(glfwGetWindowUserPointer(w));
      app->onKey(key, scancode, action, mods);
    });
  }

  void onKey(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
      if (key == GLFW_KEY_RIGHT)
        spin_a += 0.01f;
      if (key == GLFW_KEY_LEFT)
        spin_a -= 0.01f;
      if (spin_a > 0.9999f)
        spin_a = 0.9999f;
      if (spin_a < -0.9999f)
        spin_a = -0.9999f;

      if (key == GLFW_KEY_UP)
        cameraPitch += 0.05f;
      if (key == GLFW_KEY_DOWN)
        cameraPitch -= 0.05f;
      if (cameraPitch > 1.5f)
        cameraPitch = 1.5f;
      if (cameraPitch < -1.5f)
        cameraPitch = -1.5f;
    }
  }

  void onMouseButton(int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
      if (action == GLFW_PRESS)
        mouseDragging = true;
      else if (action == GLFW_RELEASE)
        mouseDragging = false;
    }
  }

  void onMouseMoved(double x, double y) {
    if (mouseDragging) {
      float dx = (float)(x - lastMouseX);
      float dy = (float)(y - lastMouseY);
      cameraYaw -= dx * 0.005f;
      cameraPitch += dy * 0.005f;
      if (cameraPitch > 1.5f)
        cameraPitch = 1.5f;
      if (cameraPitch < -1.5f)
        cameraPitch = -1.5f;
    }
    lastMouseX = x;
    lastMouseY = y;
  }

  void onScroll(double xoffset, double yoffset) {
    cameraRadius -= (float)yoffset * 1.5f;
    if (cameraRadius < 2.5f)
      cameraRadius = 2.5f;
    if (cameraRadius > 100.0f)
      cameraRadius = 100.0f;
  }

  void initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createSPHSystem();
    createGraphicsParticleDescriptorSet();
  }

  void createSPHSystem() {
    // Create particle buffer with host-visible memory for initialization
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = PARTICLE_COUNT * sizeof(float) * 12; // vec3 pos + vec3 vel + 4 floats
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &particleBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to create particle buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, particleBuffer, &memRequirements);

    // Use HOST_VISIBLE memory so we can initialize particles on CPU
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &particleBufferMemory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate particle buffer memory!");
    }

    vkBindBufferMemory(device, particleBuffer, particleBufferMemory, 0);

    // Initialize particles on CPU
    initializeParticlesOnCPU();

    // Create compute pipeline for SPH
    auto sphShaderCode = readFile("sph.spv");
    sphShaderModule = createShaderModule(sphShaderCode);

    VkPipelineShaderStageCreateInfo sphShaderStageInfo{};
    sphShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sphShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    sphShaderStageInfo.module = sphShaderModule;
    sphShaderStageInfo.pName = "main";

    // Descriptor set layout for particle buffer
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binding.descriptorCount = 1;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor set layout!");
    }

    // Allocate descriptor set
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

    VkDescriptorSetAllocateInfo allocDescInfo{};
    allocDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocDescInfo.descriptorPool = descriptorPool;
    allocDescInfo.descriptorSetCount = 1;
    allocDescInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(device, &allocDescInfo, &descriptorSet);

    // Update descriptor set with buffer
    VkDescriptorBufferInfo bufferDescriptor{};
    bufferDescriptor.buffer = particleBuffer;
    bufferDescriptor.offset = 0;
    bufferDescriptor.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferDescriptor;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    // Push constants for SPH
    VkPushConstantRange sphPushConstant{};
    sphPushConstant.offset = 0;
    sphPushConstant.size = sizeof(SPHParams);
    sphPushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &sphPushConstant;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &sphPipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create SPH pipeline layout!");
    }

    VkComputePipelineCreateInfo computePipelineInfo{};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage = sphShaderStageInfo;
    computePipelineInfo.layout = sphPipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &sphComputePipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create SPH compute pipeline!");
    }

    // Store descriptor set for later use
    sphDescriptorSet = descriptorSet;

    // Create command pool and buffer for compute
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = 0;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &commandPoolInfo, nullptr, &sphCommandPool);

    VkCommandBufferAllocateInfo commandBufferInfo{};
    commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferInfo.commandPool = sphCommandPool;
    commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &commandBufferInfo, &sphCommandBuffer);

  }

  uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }
    throw std::runtime_error("failed to find suitable memory type!");
  }

  void initializeParticlesOnCPU() {
    // Initialize particles in a disk configuration on CPU and upload to GPU
    struct ParticleData {
      float pos[3];
      float vel[3];
      float density;
      float pressure;
      float temperature;
      float mass;
    };

    std::vector<ParticleData> particles(PARTICLE_COUNT);

    float innerRadius = 3.0f;   // ISCO for a=0
    float outerRadius = 20.0f;

    for (uint32_t i = 0; i < PARTICLE_COUNT; i++) {
      // Distribute particles in disk
      float r = innerRadius + (outerRadius - innerRadius) * (float(i) / PARTICLE_COUNT);
      float phi = 2.0f * 3.14159265f * i * 0.618033988749895f; // Golden ratio

      // Disk height (thin disk)
      float height = 0.1f * (float(rand()) / RAND_MAX - 0.5f);

      particles[i].pos[0] = r * cos(phi);
      particles[i].pos[1] = height;
      particles[i].pos[2] = r * sin(phi);

      // Keplerian orbital velocity
      float omega = sqrt(1.0f / (r * r * r));
      particles[i].vel[0] = -omega * r * sin(phi);
      particles[i].vel[1] = 0.0f;
      particles[i].vel[2] = omega * r * cos(phi);

      particles[i].density = 1.0f;
      particles[i].pressure = 0.01f;
      particles[i].temperature = 1.0f;
      particles[i].mass = 0.001f;
    }

    // Map memory and upload
    void* data;
    vkMapMemory(device, particleBufferMemory, 0, sizeof(ParticleData) * PARTICLE_COUNT, 0, &data);
    memcpy(data, particles.data(), sizeof(ParticleData) * PARTICLE_COUNT);
    vkUnmapMemory(device, particleBufferMemory);
  }

  void updateSPHSimulation(float dt) {
    simTime += dt;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(sphCommandBuffer, &beginInfo);

    // Bind compute pipeline
    vkCmdBindPipeline(sphCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, sphComputePipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(sphCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, sphPipelineLayout, 0, 1, &sphDescriptorSet, 0, nullptr);

    // Update push constants
    SPHParams params{};
    params.dt = dt;
    params.time = simTime;
    params.spin_a = spin_a;
    params.mass = 1.0f;
    params.viscosity = 0.1f;
    params.alpha = 0.1f;
    params.beta = 0.0f;
    params.particleCount = PARTICLE_COUNT;
    params.innerRadius = 3.0f;  // ISCO for a=0 is ~3M
    params.outerRadius = 20.0f;

    vkCmdPushConstants(sphCommandBuffer, sphPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SPHParams), &params);

    // Dispatch SPH compute shader
    vkCmdDispatch(sphCommandBuffer, (PARTICLE_COUNT + 255) / 256, 1, 1);

    vkEndCommandBuffer(sphCommandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &sphCommandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
  }

  void createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Black Hole";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
      throw std::runtime_error("failed to create instance!");
    }
  }

  void createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create window surface!");
    }
  }

  void pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices[0]; // Just pick the first one...
  }

  void createLogicalDevice() {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    uint32_t graphicsFamily = 0; // Assume 0 supports graphics and present

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;

    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    presentQueue = graphicsQueue;
  }

  // A simplified swapchain creation
  void createSwapChain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface,
                                              &capabilities);

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
      extent = {WIDTH, HEIGHT};

    uint32_t imageCount = capabilities.minImageCount + 1;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount,
                            swapChainImages.data());

    swapChainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapChainExtent = extent;
  }

  void createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
      VkImageViewCreateInfo createInfo{};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      createInfo.image = swapChainImages[i];
      createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      createInfo.format = swapChainImageFormat;
      createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
      createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      createInfo.subresourceRange.baseMipLevel = 0;
      createInfo.subresourceRange.levelCount = 1;
      createInfo.subresourceRange.baseArrayLayer = 0;
      createInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(device, &createInfo, nullptr,
                            &swapChainImageViews[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image views!");
      }
    }
  }

  void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  VkShaderModule createShaderModule(const std::vector<char> &code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
  }

  static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
      throw std::runtime_error("failed to open file " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
  }

  void createGraphicsPipeline() {
    auto vertShaderCode = readFile("vert.spv");
    auto fragShaderCode = readFile("frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                      fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push Constants
    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(PushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Descriptor set layout for particle buffer in fragment shader
    VkDescriptorSetLayoutBinding particleBinding{};
    particleBinding.binding = 0;
    particleBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    particleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    particleBinding.descriptorCount = 1;

    VkDescriptorSetLayoutCreateInfo descLayoutInfo{};
    descLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descLayoutInfo.bindingCount = 1;
    descLayoutInfo.pBindings = &particleBinding;

    VkDescriptorSetLayout particleDescLayout;
    vkCreateDescriptorSetLayout(device, &descLayoutInfo, nullptr, &particleDescLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &particleDescLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &graphicsPipeline) != VK_SUCCESS) {
      throw std::runtime_error("failed to create graphics pipeline!");
    }

    // Create descriptor pool and set for particle buffer in graphics pipeline
    VkDescriptorPoolSize graphicsPoolSize{};
    graphicsPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    graphicsPoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo graphicsPoolInfo{};
    graphicsPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    graphicsPoolInfo.poolSizeCount = 1;
    graphicsPoolInfo.pPoolSizes = &graphicsPoolSize;
    graphicsPoolInfo.maxSets = 1;

    vkCreateDescriptorPool(device, &graphicsPoolInfo, nullptr, &graphicsParticleDescriptorPool);

    // We need to create this after SPH system is initialized, so we'll do it in initVulkan after createSPHSystem

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
  }

  void createGraphicsParticleDescriptorSet() {
    // Get the descriptor set layout from the pipeline
    VkDescriptorSetLayoutBinding particleBinding{};
    particleBinding.binding = 0;
    particleBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    particleBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    particleBinding.descriptorCount = 1;

    VkDescriptorSetLayoutCreateInfo descLayoutInfo{};
    descLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descLayoutInfo.bindingCount = 1;
    descLayoutInfo.pBindings = &particleBinding;

    VkDescriptorSetLayout particleDescLayout;
    vkCreateDescriptorSetLayout(device, &descLayoutInfo, nullptr, &particleDescLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = graphicsParticleDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &particleDescLayout;

    vkAllocateDescriptorSets(device, &allocInfo, &graphicsParticleDescriptorSet);

    // Update with particle buffer
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = particleBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descWrite{};
    descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite.dstSet = graphicsParticleDescriptorSet;
    descWrite.dstBinding = 0;
    descWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descWrite.descriptorCount = 1;
    descWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descWrite, 0, nullptr);

    vkDestroyDescriptorSetLayout(device, particleDescLayout, nullptr);
  }

  void createFramebuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());
    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
      VkImageView attachments[] = {swapChainImageViews[i]};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount = 1;
      framebufferInfo.pAttachments = attachments;
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                              &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
      }
    }
  }

  void createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = 0; // Assuming graphicsFamily is 0

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create command pool!");
    }
  }

  void createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }

  void createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                            &imageAvailableSemaphores[i]) != VK_SUCCESS ||
          vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                            &renderFinishedSemaphores[i]) != VK_SUCCESS ||
          vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) !=
              VK_SUCCESS) {
        throw std::runtime_error(
            "failed to create synchronization objects for a frame!");
      }
    }
  }

  void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphicsPipeline);

    // Bind particle buffer descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &graphicsParticleDescriptorSet, 0, nullptr);

    PushConstants constants{};
    constants.params[0] = glfwGetTime();
    constants.params[1] = WIDTH;
    constants.params[2] = HEIGHT;
    constants.params[3] = 1.0f; // Mass M

    constants.cameraPos[0] = cameraRadius * sin(cameraYaw) * cos(cameraPitch);
    constants.cameraPos[1] = cameraRadius * sin(cameraPitch);
    constants.cameraPos[2] = cameraRadius * cos(cameraYaw) * cos(cameraPitch);
    constants.cameraPos[3] = spin_a;

    vkCmdPushConstants(commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants),
                       &constants);

    // Draw full screen triangle
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      throw std::runtime_error("failed to record command buffer!");
    }
  }

  void drawFrame(ThreadPool &pool) {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                    UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      // Recreate swap chain not implemented for brevity
      return;
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    // Demonstrating CPU Multi-threading for command recording tasks
    // This distributes command buffer recordings horizontally if we had more
    // than one
    std::atomic<bool> recorded{false};

    pool.enqueue([&]() {
      recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
      recorded = true;
    });

    // wait for tasks (record parallel chunks in the future)
    while (!recorded) {
      std::this_thread::yield();
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                      inFlightFences[currentFrame]) != VK_SUCCESS) {
      throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  void mainLoop() {
    const size_t numThreads = std::thread::hardware_concurrency();
    ThreadPool threadPool(numThreads > 0 ? numThreads : 4);
    std::cout << "Running with " << numThreads << " threads...\n";
    std::cout << "SPH Particles: " << PARTICLE_COUNT << "\n";

    auto start = std::chrono::high_resolution_clock::now();
    int frames = 0;
    auto lastTime = start;

    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();

      // Update SPH simulation
      auto currentTime = std::chrono::high_resolution_clock::now();
      float dt = std::chrono::duration<float>(currentTime - lastTime).count();
      lastTime = currentTime;
      updateSPHSimulation(dt);

      drawFrame(threadPool);

      frames++;
      auto now = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> elapsed = now - start;
      if (elapsed.count() >= 1.0) {
        std::string title =
            "OpenKerr - SPH Accretion Disk - " + std::to_string(frames) + " FPS";
        glfwSetWindowTitle(window, title.c_str());
        frames = 0;
        start = now;
      }
    }
    vkDeviceWaitIdle(device);
  }

  void cleanup() {
    // SPH cleanup
    if (device != VK_NULL_HANDLE) {
      if (sphDescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, sphDescriptorPool, nullptr);
      if (sphCommandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, sphCommandPool, nullptr);
      if (sphComputePipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, sphComputePipeline, nullptr);
      if (sphPipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, sphPipelineLayout, nullptr);
      if (sphShaderModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, sphShaderModule, nullptr);
      if (particleBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, particleBuffer, nullptr);
      if (particleBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(device, particleBufferMemory, nullptr);
    }

    // Graphics cleanup
    if (device != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, commandPool, nullptr);
      for (auto framebuffer : swapChainFramebuffers)
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      vkDestroyPipeline(device, graphicsPipeline, nullptr);
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      vkDestroyRenderPass(device, renderPass, nullptr);
      for (auto imageView : swapChainImageViews)
        vkDestroyImageView(device, imageView, nullptr);
      vkDestroySwapchainKHR(device, swapChain, nullptr);
      for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
      }
      vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(instance, surface, nullptr);
      vkDestroyInstance(instance, nullptr);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
  }
};

int main() {
  BlackHoleRenderer app;
  try {
    app.run();
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}