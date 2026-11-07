// render/vulkan/vk_device.cpp — Vulkan 1.3 backend for the RHI.
//
// Design notes:
//   * Uses Vulkan 1.3 core features: dynamic rendering + synchronization2.
//   * Each frame in flight owns its own colour/depth targets, command buffer,
//     scene-constant buffer and sync objects.
//   * The scene is drawn into an RGBA8 sRGB colour target. With a window, that
//     target is blitted to the swapchain image; headless it can be read back.
//   * Memory: one allocation per buffer/image for now. A sub-allocator comes
//     later, when we have enough resources for it to matter.
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "render/rhi.h"

namespace rk::rhi {
namespace {

constexpr u32 kFramesInFlight = 2;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult vk_check_result = (call);                                         \
    if (vk_check_result != VK_SUCCESS) {                                       \
      ::rk::log_error("%s failed: VkResult %d (%s:%d)", #call,                 \
                      static_cast<int>(vk_check_result), __FILE__, __LINE__);  \
      RK_FATAL("Vulkan call failed");                                        \
    }                                                                          \
  } while (0)

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize size = 0;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

struct Frame {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence in_flight = VK_NULL_HANDLE;
  VkSemaphore image_available = VK_NULL_HANDLE;
  Buffer scene_constants;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  Image color;
  Image depth;
  Image shadow;            // sun/moon shadow map (fixed size, not resized)
  bool shadow_done = false;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* user) {
  auto* error_count = static_cast<u32*>(user);
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++*error_count;
    log_error("vulkan: %s", data->pMessage);
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    log_warn("vulkan: %s", data->pMessage);
  }
  return VK_FALSE;
}

bool has_layer(const char* name) {
  u32 count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  for (const auto& l : layers)
    if (std::strcmp(l.layerName, name) == 0) return true;
  return false;
}

bool has_instance_extension(const char* name) {
  u32 count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
  for (const auto& e : exts)
    if (std::strcmp(e.extensionName, name) == 0) return true;
  return false;
}

bool has_device_extension(VkPhysicalDevice gpu, const char* name) {
  u32 count = 0;
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, exts.data());
  for (const auto& e : exts)
    if (std::strcmp(e.extensionName, name) == 0) return true;
  return false;
}

VkFormat to_vk(AttribFormat f) {
  switch (f) {
    case AttribFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case AttribFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case AttribFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                   VkImageLayout old_layout, VkImageLayout new_layout,
                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                   VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = src_stage;
  b.srcAccessMask = src_access;
  b.dstStageMask = dst_stage;
  b.dstAccessMask = dst_access;
  b.oldLayout = old_layout;
  b.newLayout = new_layout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

class VulkanDevice final : public Device {
 public:
  bool init(const DeviceDesc& desc);
  ~VulkanDevice() override;

  const char* device_name() const override { return gpu_name_.c_str(); }
  BufferHandle create_buffer(BufferUsage usage, const void* data, size_t bytes) override;
  PipelineHandle create_pipeline(const PipelineDesc& desc) override;
  bool begin_frame() override;
  void render_size(u32& width, u32& height) const override {
    width = extent_.width;
    height = extent_.height;
  }
  void set_scene_constants(const void* data, size_t bytes) override;
  void begin_pass(const float clear_rgba[4]) override;
  void bind_pipeline(PipelineHandle pipeline) override;
  void push_constants(const void* data, u32 bytes) override;
  void bind_vertex_buffer(BufferHandle buffer) override;
  void bind_index_buffer(BufferHandle buffer) override;
  void draw(u32 vertex_count) override;
  void draw_indexed(u32 index_count) override;
  void end_pass() override;
  void end_frame() override;
  bool capture_last_frame(std::vector<u8>& rgba, u32& width, u32& height) override;
  void wait_idle() override { vkDeviceWaitIdle(device_); }
  u32 validation_error_count() const override { return validation_errors_; }
  void begin_shadow_pass() override;
  u32 shadow_map_size() const override { return shadow_size_; }

 private:
  bool create_instance(const DeviceDesc& desc);
  bool pick_gpu();
  bool create_device();
  bool create_swapchain();
  void destroy_swapchain();
  bool create_targets();
  void destroy_targets();
  bool recreate_for_resize();
  u32 find_memory_type(u32 type_bits, VkMemoryPropertyFlags props) const;
  Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
  void destroy_buffer(Buffer& b);
  Image make_image(u32 w, u32 h, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
  void destroy_image(Image& img);

  DeviceDesc desc_;
  bool headless_ = false;
  u32 validation_errors_ = 0;
  std::string gpu_name_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  u32 queue_family_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  bool portability_subset_ = false;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkSemaphore> render_finished_;  // one per swapchain image
  VkExtent2D extent_{};

  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  std::array<Frame, kFramesInFlight> frames_{};
  u32 shadow_size_ = 2048;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  bool in_shadow_pass_ = false;
  u32 frame_index_ = 0;
  u32 swapchain_image_index_ = 0;
  i32 last_finished_frame_ = -1;
  bool frame_active_ = false;

  std::vector<Buffer> buffers_{Buffer{}};          // index 0 = invalid handle
  std::vector<VkPipeline> pipelines_{VK_NULL_HANDLE};
};

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool VulkanDevice::init(const DeviceDesc& desc) {
  desc_ = desc;
  headless_ = desc.headless;
  if (!create_instance(desc)) return false;
  if (!headless_) {
    u64 surface = 0;
    if (!desc.create_surface || !desc.create_surface(instance_, &surface)) return false;
    surface_ = reinterpret_cast<VkSurfaceKHR>(surface);
  }
  if (!pick_gpu()) return false;
  if (!create_device()) return false;

  // Scene constants: one uniform buffer per frame, visible to all shaders.
  // Binding 1: the frame's shadow map, sampled with depth comparison.
  VkDescriptorSetLayoutBinding bindings[2]{};
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_info.bindingCount = 2;
  set_info.pBindings = bindings;
  VK_CHECK(vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout_));

  VkDescriptorPoolSize pool_sizes[2] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight}};
  VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = kFramesInFlight;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  VK_CHECK(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_));

  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           kMaxPushConstantsBytes};
  VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &set_layout_;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push;
  VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_));

  // Shadow sampling: hardware depth comparison, smooth (linear) where supported,
  // and a white border so anything outside the map counts as lit.
  shadow_size_ = std::clamp(desc.shadow_map_size, 256u, 8192u);
  VkFormatProperties depth_props;
  vkGetPhysicalDeviceFormatProperties(gpu_, kDepthFormat, &depth_props);
  bool linear = (depth_props.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
  VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  sampler_info.minFilter = sampler_info.magFilter;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  sampler_info.compareEnable = VK_TRUE;
  sampler_info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  sampler_info.maxLod = 0.0f;
  VK_CHECK(vkCreateSampler(device_, &sampler_info, nullptr, &shadow_sampler_));

  for (Frame& f : frames_) {
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cp.queueFamilyIndex = queue_family_;
    VK_CHECK(vkCreateCommandPool(device_, &cp, nullptr, &f.pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = f.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device_, &ca, &f.cmd));

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(device_, &fi, nullptr, &f.in_flight));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &f.image_available));

    f.scene_constants = make_buffer(kMaxSceneConstantsBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memset(f.scene_constants.mapped, 0, kMaxSceneConstantsBytes);

    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descriptor_pool_;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &set_layout_;
    VK_CHECK(vkAllocateDescriptorSets(device_, &da, &f.descriptor_set));
    f.shadow = make_image(shadow_size_, shadow_size_, kDepthFormat,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);

    VkDescriptorBufferInfo bi{f.scene_constants.buffer, 0, kMaxSceneConstantsBytes};
    VkDescriptorImageInfo ii{shadow_sampler_, f.shadow.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstSet = f.descriptor_set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &bi;
    w[1].dstSet = f.descriptor_set;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
  }

  if (headless_) {
    extent_ = {desc.width, desc.height};
  } else if (!create_swapchain()) {
    return false;
  }
  if (extent_.width > 0 && extent_.height > 0 && !create_targets()) return false;
  log_info("vulkan device ready: %s (%ux%u, %s)", gpu_name_.c_str(), extent_.width,
           extent_.height, headless_ ? "headless" : "windowed");
  return true;
}

bool VulkanDevice::create_instance(const DeviceDesc& desc) {
  u32 version = 0;
  vkEnumerateInstanceVersion(&version);
  if (version < VK_API_VERSION_1_3) {
    log_error("Vulkan 1.3 is required (loader reports %u.%u)", VK_API_VERSION_MAJOR(version),
              VK_API_VERSION_MINOR(version));
    return false;
  }

  std::vector<const char*> extensions;
  if (!desc.headless) extensions = desc.instance_extensions;
  std::vector<const char*> layers;
  bool want_debug = false;
  if (desc.validation) {
    if (has_layer("VK_LAYER_KHRONOS_validation")) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      want_debug = has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      if (want_debug) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      log_warn("validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }
  }
  VkInstanceCreateFlags flags = 0;
  // Needed for MoltenVK (macOS development builds).
  if (has_instance_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Rakshas";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "Rakshas Engine";
  app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  app.apiVersion = VK_API_VERSION_1_3;

  VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dbg.pfnUserCallback = debug_callback;
  dbg.pUserData = &validation_errors_;

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pNext = want_debug ? &dbg : nullptr;
  ci.flags = flags;
  ci.pApplicationInfo = &app;
  ci.enabledLayerCount = static_cast<u32>(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  ci.enabledExtensionCount = static_cast<u32>(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
  if (r != VK_SUCCESS) {
    log_error("vkCreateInstance failed: %d", static_cast<int>(r));
    return false;
  }
  if (want_debug) {
    auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create_fn) VK_CHECK(create_fn(instance_, &dbg, nullptr, &messenger_));
    log_info("vulkan validation layers enabled");
  }
  return true;
}

bool VulkanDevice::pick_gpu() {
  u32 count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  std::vector<VkPhysicalDevice> gpus(count);
  vkEnumeratePhysicalDevices(instance_, &count, gpus.data());

  int best_score = -1;
  std::string found_report;
  for (VkPhysicalDevice gpu : gpus) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    found_report += std::string("\n  - ") + props.deviceName + " (Vulkan " +
                    std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(props.apiVersion)) + ")";
    if (props.apiVersion < VK_API_VERSION_1_3) continue;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(gpu, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2) continue;
    if (!headless_ && !has_device_extension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

    u32 qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qcount, qprops.data());
    i32 family = -1;
    for (u32 i = 0; i < qcount; ++i) {
      if (!(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
      if (!headless_) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &present);
        if (!present) continue;
      }
      family = static_cast<i32>(i);
      break;
    }
    if (family < 0) continue;

    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 3;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
    if (score > best_score) {
      best_score = score;
      gpu_ = gpu;
      queue_family_ = static_cast<u32>(family);
      gpu_name_ = props.deviceName;
    }
  }
  if (!gpu_) {
    log_error(
        "No graphics card with Vulkan 1.3 support was found.\n"
        "Graphics found:%s\n"
        "Please update your graphics driver (AMD: amd.com/support, NVIDIA: nvidia.com/drivers,\n"
        "Intel: intel.com/support) and try again.",
        found_report.empty() ? "\n  (none)" : found_report.c_str());
    return false;
  }
  return true;
}

bool VulkanDevice::create_device() {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = queue_family_;
  qi.queueCount = 1;
  qi.pQueuePriorities = &priority;

  std::vector<const char*> extensions;
  if (!headless_) extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  if (has_device_extension(gpu_, "VK_KHR_portability_subset")) {
    extensions.push_back("VK_KHR_portability_subset");
    portability_subset_ = true;
  }

  VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  f13.synchronization2 = VK_TRUE;
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &f13;

  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.pNext = &f2;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qi;
  ci.enabledExtensionCount = static_cast<u32>(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  VkResult r = vkCreateDevice(gpu_, &ci, nullptr, &device_);
  if (r != VK_SUCCESS) {
    log_error("vkCreateDevice failed: %d", static_cast<int>(r));
    return false;
  }
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
  return true;
}

// ---------------------------------------------------------------------------
// Swapchain and render targets
// ---------------------------------------------------------------------------

bool VulkanDevice::create_swapchain() {
  VkSurfaceCapabilitiesKHR caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps));

  u32 w = 0, h = 0;
  if (desc_.drawable_size) desc_.drawable_size(w, h);
  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    w = caps.currentExtent.width;
    h = caps.currentExtent.height;
  }
  w = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
  h = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
  extent_ = {w, h};
  if (w == 0 || h == 0) return true;  // minimised: try again later

  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
    log_error("swapchain images cannot be transfer destinations on this device");
    return false;
  }

  u32 fcount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fcount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fcount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &fcount, formats.data());
  VkSurfaceFormatKHR chosen = formats.at(0);
  for (const auto& f : formats) {
    if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }
  }
  swapchain_format_ = chosen.format;

  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always available, vsync on
  if (!desc_.vsync) {
    u32 mcount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mcount, nullptr);
    std::vector<VkPresentModeKHR> modes(mcount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &mcount, modes.data());
    for (auto m : modes)
      if (m == VK_PRESENT_MODE_MAILBOX_KHR || m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
  }

  u32 image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = image_count;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = extent_;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

  u32 count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  swapchain_images_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchain_images_.data());
  render_finished_.resize(count);
  for (auto& s : render_finished_) {
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &s));
  }
  return true;
}

void VulkanDevice::destroy_swapchain() {
  for (auto s : render_finished_) vkDestroySemaphore(device_, s, nullptr);
  render_finished_.clear();
  swapchain_images_.clear();
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

bool VulkanDevice::create_targets() {
  for (Frame& f : frames_) {
    f.color = make_image(extent_.width, extent_.height, kColorFormat,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
    f.depth = make_image(extent_.width, extent_.height, kDepthFormat,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
  }
  last_finished_frame_ = -1;
  return true;
}

void VulkanDevice::destroy_targets() {
  for (Frame& f : frames_) {
    destroy_image(f.color);
    destroy_image(f.depth);
  }
}

bool VulkanDevice::recreate_for_resize() {
  vkDeviceWaitIdle(device_);
  destroy_targets();
  destroy_swapchain();
  if (!create_swapchain()) return false;
  if (extent_.width == 0 || extent_.height == 0) return false;
  return create_targets();
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

u32 VulkanDevice::find_memory_type(u32 type_bits, VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mem;
  vkGetPhysicalDeviceMemoryProperties(gpu_, &mem);
  for (u32 i = 0; i < mem.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  RK_FATAL("no suitable Vulkan memory type");
}

Buffer VulkanDevice::make_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags props) {
  Buffer b;
  b.size = size;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = size;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(device_, &ci, nullptr, &b.buffer));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device_, b.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, props);
  VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &b.memory));
  VK_CHECK(vkBindBufferMemory(device_, b.buffer, b.memory, 0));
  if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    VK_CHECK(vkMapMemory(device_, b.memory, 0, size, 0, &b.mapped));
  return b;
}

void VulkanDevice::destroy_buffer(Buffer& b) {
  if (b.mapped) vkUnmapMemory(device_, b.memory);
  if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
  if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
  b = Buffer{};
}

Image VulkanDevice::make_image(u32 w, u32 h, VkFormat format, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect) {
  Image img;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = format;
  ci.extent = {w, h, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(device_, &ci, nullptr, &img.image));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(device_, img.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &img.memory));
  VK_CHECK(vkBindImageMemory(device_, img.image, img.memory, 0));
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = format;
  vi.subresourceRange = {aspect, 0, 1, 0, 1};
  VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &img.view));
  return img;
}

void VulkanDevice::destroy_image(Image& img) {
  if (img.view) vkDestroyImageView(device_, img.view, nullptr);
  if (img.image) vkDestroyImage(device_, img.image, nullptr);
  if (img.memory) vkFreeMemory(device_, img.memory, nullptr);
  img = Image{};
}

BufferHandle VulkanDevice::create_buffer(BufferUsage usage, const void* data, size_t bytes) {
  RK_ASSERT(bytes > 0);
  VkBufferUsageFlags flags = usage == BufferUsage::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                          : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  // Rung 1: host-visible memory. Device-local + staging uploads come later.
  Buffer b = make_buffer(bytes, flags,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (data) std::memcpy(b.mapped, data, bytes);
  buffers_.push_back(b);
  return BufferHandle{static_cast<u32>(buffers_.size() - 1)};
}

PipelineHandle VulkanDevice::create_pipeline(const PipelineDesc& desc) {
  auto make_module = [&](const std::vector<u32>& code) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size() * sizeof(u32);
    ci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &m));
    return m;
  };
  const bool shadow = desc.pass == PassKind::Shadow;
  VkShaderModule vs = make_module(desc.vertex_spirv);
  VkShaderModule fs = desc.fragment_spirv.empty() ? VK_NULL_HANDLE : make_module(desc.fragment_spirv);

  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{0, desc.vertex_stride, VK_VERTEX_INPUT_RATE_VERTEX};
  std::vector<VkVertexInputAttributeDescription> attrs;
  for (const auto& a : desc.attributes) attrs.push_back({a.location, 0, to_vk(a.format), a.offset});
  VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  if (desc.vertex_stride > 0) {
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = static_cast<u32>(attrs.size());
    vin.pVertexAttributeDescriptions = attrs.data();
  }

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = desc.cull_back_faces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
  // Our projection flips Y, which keeps counter-clockwise triangles front-facing.
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  if (shadow) {
    // Push depth slightly away from the light to avoid speckled "shadow acne".
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.0f;
    rs.depthBiasSlopeFactor = 2.5f;
  }

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = shadow ? 0 : 1;
  cb.pAttachments = shadow ? nullptr : &blend;

  VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dyn_states;

  VkFormat color_format = kColorFormat;
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = shadow ? 0 : 1;
  rendering.pColorAttachmentFormats = shadow ? nullptr : &color_format;
  rendering.depthAttachmentFormat = kDepthFormat;

  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.pNext = &rendering;
  ci.stageCount = fs ? 2 : 1;
  ci.pStages = stages;
  ci.pVertexInputState = &vin;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pDepthStencilState = &ds;
  ci.pColorBlendState = &cb;
  ci.pDynamicState = &dyn;
  ci.layout = pipeline_layout_;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline);
  vkDestroyShaderModule(device_, vs, nullptr);
  if (fs) vkDestroyShaderModule(device_, fs, nullptr);
  if (r != VK_SUCCESS) {
    log_error("failed to create pipeline '%s': %d", desc.debug_name, static_cast<int>(r));
    return {};
  }
  pipelines_.push_back(pipeline);
  return PipelineHandle{static_cast<u32>(pipelines_.size() - 1)};
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

bool VulkanDevice::begin_frame() {
  RK_ASSERT(!frame_active_);
  if (!headless_ && (extent_.width == 0 || extent_.height == 0 || swapchain_ == VK_NULL_HANDLE)) {
    if (!recreate_for_resize()) return false;
  }
  Frame& f = frames_[frame_index_];
  VK_CHECK(vkWaitForFences(device_, 1, &f.in_flight, VK_TRUE, UINT64_MAX));

  if (!headless_) {
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.image_available,
                                       VK_NULL_HANDLE, &swapchain_image_index_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
      recreate_for_resize();
      return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
      log_error("vkAcquireNextImageKHR failed: %d", static_cast<int>(r));
      return false;
    }
  }

  VK_CHECK(vkResetFences(device_, 1, &f.in_flight));
  VK_CHECK(vkResetCommandPool(device_, f.pool, 0));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));

  image_barrier(f.cmd, f.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  image_barrier(f.cmd, f.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  f.shadow_done = false;
  frame_active_ = true;
  return true;
}

void VulkanDevice::begin_shadow_pass() {
  RK_ASSERT(frame_active_ && !in_shadow_pass_);
  Frame& f = frames_[frame_index_];
  image_barrier(f.cmd, f.shadow.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = f.shadow.view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, {shadow_size_, shadow_size_}};
  ri.layerCount = 1;
  ri.pDepthAttachment = &depth;
  vkCmdBeginRendering(f.cmd, &ri);
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(shadow_size_),
                      static_cast<float>(shadow_size_), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, {shadow_size_, shadow_size_}};
  vkCmdSetViewport(f.cmd, 0, 1, &viewport);
  vkCmdSetScissor(f.cmd, 0, 1, &scissor);
  in_shadow_pass_ = true;
  f.shadow_done = true;
}

void VulkanDevice::set_scene_constants(const void* data, size_t bytes) {
  RK_ASSERT(frame_active_ && bytes <= kMaxSceneConstantsBytes);
  std::memcpy(frames_[frame_index_].scene_constants.mapped, data, bytes);
}

void VulkanDevice::begin_pass(const float clear_rgba[4]) {
  Frame& f = frames_[frame_index_];
  if (!f.shadow_done) {  // no shadow casters this frame: clear the map to "fully lit"
    begin_shadow_pass();
    end_pass();
  }
  VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = f.color.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  std::memcpy(color.clearValue.color.float32, clear_rgba, sizeof(float) * 4);
  VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = f.depth.view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, extent_};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &color;
  ri.pDepthAttachment = &depth;
  vkCmdBeginRendering(f.cmd, &ri);

  VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent_.width),
                      static_cast<float>(extent_.height), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, extent_};
  vkCmdSetViewport(f.cmd, 0, 1, &viewport);
  vkCmdSetScissor(f.cmd, 0, 1, &scissor);
}

void VulkanDevice::bind_pipeline(PipelineHandle pipeline) {
  RK_ASSERT(pipeline.valid() && pipeline.id < pipelines_.size());
  Frame& f = frames_[frame_index_];
  vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[pipeline.id]);
  vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                          &f.descriptor_set, 0, nullptr);
}

void VulkanDevice::push_constants(const void* data, u32 bytes) {
  RK_ASSERT(bytes <= kMaxPushConstantsBytes);
  vkCmdPushConstants(frames_[frame_index_].cmd, pipeline_layout_,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, bytes, data);
}

void VulkanDevice::bind_vertex_buffer(BufferHandle buffer) {
  RK_ASSERT(buffer.valid() && buffer.id < buffers_.size());
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(frames_[frame_index_].cmd, 0, 1, &buffers_[buffer.id].buffer, &offset);
}

void VulkanDevice::bind_index_buffer(BufferHandle buffer) {
  RK_ASSERT(buffer.valid() && buffer.id < buffers_.size());
  vkCmdBindIndexBuffer(frames_[frame_index_].cmd, buffers_[buffer.id].buffer, 0,
                       VK_INDEX_TYPE_UINT32);
}

void VulkanDevice::draw(u32 vertex_count) {
  vkCmdDraw(frames_[frame_index_].cmd, vertex_count, 1, 0, 0);
}

void VulkanDevice::draw_indexed(u32 index_count) {
  vkCmdDrawIndexed(frames_[frame_index_].cmd, index_count, 1, 0, 0, 0);
}

void VulkanDevice::end_pass() {
  Frame& f = frames_[frame_index_];
  vkCmdEndRendering(f.cmd);
  if (in_shadow_pass_) {
    image_barrier(f.cmd, f.shadow.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    in_shadow_pass_ = false;
  }
}

void VulkanDevice::end_frame() {
  RK_ASSERT(frame_active_);
  Frame& f = frames_[frame_index_];

  image_barrier(f.cmd, f.color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);

  if (!headless_) {
    VkImage target = swapchain_images_[swapchain_image_index_];
    image_barrier(f.cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {static_cast<i32>(extent_.width), static_cast<i32>(extent_.height), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = region.srcOffsets[1];
    vkCmdBlitImage(f.cmd, f.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
    image_barrier(f.cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
  }
  VK_CHECK(vkEndCommandBuffer(f.cmd));

  VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmd_info.commandBuffer = f.cmd;
  VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cmd_info;
  if (!headless_) {
    wait.semaphore = f.image_available;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    signal.semaphore = render_finished_[swapchain_image_index_];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
  }
  VK_CHECK(vkQueueSubmit2(queue_, 1, &submit, f.in_flight));
  last_finished_frame_ = static_cast<i32>(frame_index_);

  if (!headless_) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[swapchain_image_index_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &swapchain_image_index_;
    VkResult r = vkQueuePresentKHR(queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
      recreate_for_resize();
    } else if (r != VK_SUCCESS) {
      log_error("vkQueuePresentKHR failed: %d", static_cast<int>(r));
    }
  }

  frame_index_ = (frame_index_ + 1) % kFramesInFlight;
  frame_active_ = false;
}

bool VulkanDevice::capture_last_frame(std::vector<u8>& rgba, u32& width, u32& height) {
  if (last_finished_frame_ < 0) return false;
  vkDeviceWaitIdle(device_);
  Frame& f = frames_[static_cast<u32>(last_finished_frame_)];
  width = extent_.width;
  height = extent_.height;
  VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
  Buffer readback = make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  VK_CHECK(vkResetCommandPool(device_, f.pool, 0));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));
  // The colour target was left in TRANSFER_SRC_OPTIMAL by end_frame().
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyImageToBuffer(f.cmd, f.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &region);
  VK_CHECK(vkEndCommandBuffer(f.cmd));
  VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmd_info.commandBuffer = f.cmd;
  VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cmd_info;
  VK_CHECK(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE));
  vkQueueWaitIdle(queue_);

  rgba.resize(static_cast<size_t>(bytes));
  std::memcpy(rgba.data(), readback.mapped, static_cast<size_t>(bytes));
  destroy_buffer(readback);
  return true;
}

VulkanDevice::~VulkanDevice() {
  if (!device_) {
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
    return;
  }
  vkDeviceWaitIdle(device_);
  for (size_t i = 1; i < pipelines_.size(); ++i)
    if (pipelines_[i]) vkDestroyPipeline(device_, pipelines_[i], nullptr);
  for (size_t i = 1; i < buffers_.size(); ++i) destroy_buffer(buffers_[i]);
  destroy_targets();
  for (Frame& f : frames_) {
    destroy_image(f.shadow);
    destroy_buffer(f.scene_constants);
    if (f.in_flight) vkDestroyFence(device_, f.in_flight, nullptr);
    if (f.image_available) vkDestroySemaphore(device_, f.image_available, nullptr);
    if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
  }
  destroy_swapchain();
  if (shadow_sampler_) vkDestroySampler(device_, shadow_sampler_, nullptr);
  if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
  if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
  if (set_layout_) vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
  vkDestroyDevice(device_, nullptr);
  if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (messenger_) {
    auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_fn) destroy_fn(instance_, messenger_, nullptr);
  }
  vkDestroyInstance(instance_, nullptr);
}

}  // namespace

std::unique_ptr<Device> create_vulkan_device(const DeviceDesc& desc) {
  auto device = std::make_unique<VulkanDevice>();
  if (!device->init(desc)) return nullptr;
  return device;
}

}  // namespace rk::rhi
