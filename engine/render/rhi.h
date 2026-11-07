// render/rhi.h — the render translation layer ("RHI": render hardware interface).
//
// Game and renderer code talk ONLY to this interface. Each graphics API gets a
// backend that implements it: Vulkan today (render/vulkan/), PlayStation and
// Xbox backends later. Keep this interface small and boring: every operation
// added here must be implemented by every live backend.
//
// Model for now (rung 1):
//   * One frame = begin_frame -> begin_pass -> draws -> end_pass -> end_frame.
//   * Every pipeline sees the same per-frame "scene constants" block
//     (GLSL: set 0, binding 0, std140) and up to 128 bytes of push constants.
//   * Rendering happens into an internal colour target (sRGB) + depth target.
//     With a window it is copied to the screen; headless it can be read back.
#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "core/types.h"

namespace rk::rhi {

struct BufferHandle {
  u32 id = 0;
  bool valid() const { return id != 0; }
};
struct PipelineHandle {
  u32 id = 0;
  bool valid() const { return id != 0; }
};

enum class BufferUsage { Vertex, Index32 };

enum class AttribFormat { Float2, Float3, Float4 };

struct VertexAttribute {
  u32 location = 0;
  AttribFormat format = AttribFormat::Float3;
  u32 offset = 0;
};

struct PipelineDesc {
  const char* debug_name = "pipeline";
  std::vector<u32> vertex_spirv;
  std::vector<u32> fragment_spirv;
  u32 vertex_stride = 0;                     // 0 = no vertex buffer
  std::vector<VertexAttribute> attributes;
  bool depth_test = true;
  bool depth_write = true;
  bool cull_back_faces = true;               // counter-clockwise = front
};

struct DeviceDesc {
  bool validation = false;                   // enable API validation layers
  bool headless = false;                     // no window; render offscreen only
  bool vsync = true;
  u32 width = 1280;                          // headless render size
  u32 height = 720;
  // Window integration (ignored when headless):
  std::vector<const char*> instance_extensions;
  std::function<bool(void* instance, u64* out_surface)> create_surface;
  std::function<void(u32& width, u32& height)> drawable_size;
};

constexpr u32 kMaxSceneConstantsBytes = 256;
constexpr u32 kMaxPushConstantsBytes = 128;

class Device {
 public:
  virtual ~Device() = default;

  virtual const char* device_name() const = 0;

  virtual BufferHandle create_buffer(BufferUsage usage, const void* data, size_t bytes) = 0;
  virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;

  // Returns false when no frame should be drawn (e.g. window minimised).
  virtual bool begin_frame() = 0;
  virtual void render_size(u32& width, u32& height) const = 0;
  virtual void set_scene_constants(const void* data, size_t bytes) = 0;
  virtual void begin_pass(const float clear_rgba[4]) = 0;
  virtual void bind_pipeline(PipelineHandle pipeline) = 0;
  virtual void push_constants(const void* data, u32 bytes) = 0;
  virtual void bind_vertex_buffer(BufferHandle buffer) = 0;
  virtual void bind_index_buffer(BufferHandle buffer) = 0;
  virtual void draw(u32 vertex_count) = 0;
  virtual void draw_indexed(u32 index_count) = 0;
  virtual void end_pass() = 0;
  virtual void end_frame() = 0;

  // Copies the most recently finished frame as tightly packed RGBA8 (sRGB).
  virtual bool capture_last_frame(std::vector<u8>& rgba, u32& width, u32& height) = 0;

  virtual void wait_idle() = 0;

  // Number of errors reported by the validation layers so far.
  virtual u32 validation_error_count() const = 0;
};

// Backends ------------------------------------------------------------------
std::unique_ptr<Device> create_vulkan_device(const DeviceDesc& desc);

}  // namespace rk::rhi
