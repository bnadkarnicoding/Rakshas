// render/rhi.h — the render translation layer ("RHI": render hardware interface).
//
// Game and renderer code talk ONLY to this interface. Each graphics API gets a
// backend that implements it: Vulkan today (render/vulkan/), PlayStation and
// Xbox backends later. Keep this interface small and boring: every operation
// added here must be implemented by every live backend.
//
// Model for now (rung 2):
//   * One frame = begin_frame -> [begin_shadow_pass -> draws -> end_pass]
//                 -> begin_pass -> draws -> end_pass -> end_frame.
//   * Every pipeline sees the same per-frame "scene constants" block
//     (GLSL: set 0, binding 0, std140) and up to 128 bytes of push constants.
//   * The shadow pass renders depth from the sun/moon into the frame's shadow
//     map, which main-pass shaders read at set 0, binding 1 (sampler2DShadow,
//     white border = fully lit outside the map). If a frame skips the shadow
//     pass, the map is cleared so nothing is shadowed. This is deliberately
//     a single built-in shadow map; general render targets come later.
//   * Textured pipelines read a material texture at set 1, binding 0
//     (sampler2D, repeat, mipmapped), chosen with bind_texture().
//   * Instanced draws (trees, grass) read per-instance data from a second
//     vertex buffer (binding 1) set with bind_instance_buffer().
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
struct TextureHandle {
  u32 id = 0;
  bool valid() const { return id != 0; }
};

enum class BufferUsage { Vertex, Index32, Instance };

struct TextureDesc {
  const char* debug_name = "texture";
  u32 width = 0;
  u32 height = 0;
  const u8* rgba = nullptr;   // width * height * 4 bytes
  bool srgb = true;           // colour images are sRGB; data maps (later) are not
  bool mipmaps = true;        // smaller copies for distant surfaces (less shimmer)
};

enum class AttribFormat { Float2, Float3, Float4 };

enum class PassKind { Main, Shadow };

struct VertexAttribute {
  u32 location = 0;
  AttribFormat format = AttribFormat::Float3;
  u32 offset = 0;
};

struct PipelineDesc {
  const char* debug_name = "pipeline";
  PassKind pass = PassKind::Main;            // Shadow = depth only, no fragment shader needed
  std::vector<u32> vertex_spirv;
  std::vector<u32> fragment_spirv;           // may be empty for shadow pipelines
  u32 vertex_stride = 0;                     // 0 = no vertex buffer
  std::vector<VertexAttribute> attributes;
  u32 instance_stride = 0;                   // > 0 = per-instance data at binding 1
  std::vector<VertexAttribute> instance_attributes;
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
  u32 shadow_map_size = 2048;                // 1024 for the low preset
  // Window integration (ignored when headless):
  std::vector<const char*> instance_extensions;
  std::function<bool(void* instance, u64* out_surface)> create_surface;
  std::function<void(u32& width, u32& height)> drawable_size;
};

constexpr u32 kMaxSceneConstantsBytes = 512;
constexpr u32 kMaxPushConstantsBytes = 128;

class Device {
 public:
  virtual ~Device() = default;

  virtual const char* device_name() const = 0;

  virtual BufferHandle create_buffer(BufferUsage usage, const void* data, size_t bytes) = 0;
  // A buffer rewritten every frame (e.g. animated characters). Call
  // update_buffer after begin_frame, before drawing with it.
  virtual BufferHandle create_dynamic_buffer(BufferUsage usage, size_t bytes) = 0;
  virtual void update_buffer(BufferHandle buffer, const void* data, size_t bytes) = 0;
  virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
  virtual TextureHandle create_texture(const TextureDesc& desc) = 0;

  // Returns false when no frame should be drawn (e.g. window minimised).
  virtual bool begin_frame() = 0;
  virtual void render_size(u32& width, u32& height) const = 0;
  virtual void set_scene_constants(const void* data, size_t bytes) = 0;
  virtual void begin_shadow_pass() = 0;
  virtual void begin_pass(const float clear_rgba[4]) = 0;
  virtual void bind_pipeline(PipelineHandle pipeline) = 0;
  virtual void push_constants(const void* data, u32 bytes) = 0;
  virtual void bind_vertex_buffer(BufferHandle buffer) = 0;
  virtual void bind_index_buffer(BufferHandle buffer) = 0;
  virtual void bind_instance_buffer(BufferHandle buffer) = 0;
  virtual void bind_texture(TextureHandle texture) = 0;
  virtual void draw(u32 vertex_count) = 0;
  virtual void draw_indexed(u32 index_count) = 0;
  virtual void draw_indexed_instanced(u32 index_count, u32 instance_count) = 0;
  virtual void end_pass() = 0;               // ends either kind of pass
  virtual void end_frame() = 0;

  // Copies the most recently finished frame as tightly packed RGBA8 (sRGB).
  virtual bool capture_last_frame(std::vector<u8>& rgba, u32& width, u32& height) = 0;

  virtual void wait_idle() = 0;

  // Number of errors reported by the validation layers so far.
  virtual u32 validation_error_count() const = 0;
  virtual u32 shadow_map_size() const = 0;
};

// Backends ------------------------------------------------------------------
std::unique_ptr<Device> create_vulkan_device(const DeviceDesc& desc);

}  // namespace rk::rhi
