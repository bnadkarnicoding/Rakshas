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

// Input gathered once per frame, already mapped from keyboard, mouse and gamepad.
struct InputState {
  bool quit = false;           // Esc / gamepad Back
  bool resized = false;
  bool toggle_pause = false;   // P / gamepad Y (North): pause the sun
  bool screenshot = false;     // F12
  bool toggle_camera_mode = false;
  bool toggle_mute = false;    // M  // C / right stick click: nudge vs fully directed zones
  float move_x = 0.0f;         // -1..1 right   (D/A, left stick X)
  float move_y = 0.0f;         // -1..1 forward (W/S, left stick Y)
  bool run = false;            // Shift / left bumper
  float look_x = 0.0f;         // -1..1 (Left/Right arrows, right stick X): + turns right
  float look_y = 0.0f;         // -1..1 (Up/Down arrows, right stick Y):   + looks up
  float mouse_dx = 0.0f;       // pixels dragged with the right mouse button this frame
  float mouse_dy = 0.0f;
  float zoom = 0.0f;           // + moves the camera further away (wheel, D-pad, PageUp/Down)
  float time_scrub = 0.0f;     // -1..1 (Q/E, triggers)
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
