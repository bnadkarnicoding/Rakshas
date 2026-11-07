// platform/platform.h — the window and raw input, with no SDL types exposed.
//
// Everything operating-system specific lives behind this interface (and the
// RHI backends). The rest of the engine never includes SDL headers.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"

namespace rk {

// Input gathered once per frame, already mapped from keyboard + gamepad.
struct InputState {
  bool quit = false;
  bool resized = false;
  bool toggle_pause = false;   // Space / gamepad South
  bool screenshot = false;     // F12
  float orbit = 0.0f;          // -1..1  (A/D, Left/Right, left stick X)
  float zoom = 0.0f;           // -1..1  (W/S, Up/Down, left stick Y)
  float tilt = 0.0f;           // -1..1  (R/F, right stick Y): + looks up
  float time_scrub = 0.0f;     // -1..1  (Q/E, triggers)
};

struct WindowDesc {
  std::string title = "Rakshas";
  u32 width = 1280;
  u32 height = 720;
};

class Window {
 public:
  virtual ~Window() = default;

  // Pumps OS events and fills `input`. Call once per frame.
  virtual void poll(InputState& input) = 0;

  // Size of the drawable area in pixels (may differ from window size on HiDPI).
  virtual void drawable_size(u32& width, u32& height) const = 0;

  // Vulkan instance extensions the window system needs.
  virtual std::vector<const char*> vulkan_instance_extensions() const = 0;

  // Creates a VkSurfaceKHR. `instance` is a VkInstance; `out_surface` receives
  // the VkSurfaceKHR handle. Kept opaque so this header stays API-neutral.
  virtual bool create_vulkan_surface(void* instance, u64* out_surface) = 0;

  static std::unique_ptr<Window> create(const WindowDesc& desc);
};

// Folder containing the running executable (ends with a path separator).
std::string executable_dir();

// High-resolution time in seconds since an arbitrary start point.
f64 time_seconds();

}  // namespace rk
