// platform/platform_sdl.cpp — Window + input implemented with SDL3.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "core/log.h"
#include "core/math.h"
#include "platform/platform.h"

namespace rk {
namespace {

float axis_value(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  float v = static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0f;
  const float dead_zone = 0.2f;
  if (std::fabs(v) < dead_zone) return 0.0f;
  return clampf(v, -1.0f, 1.0f);
}

class SdlWindow final : public Window {
 public:
  explicit SdlWindow(SDL_Window* window) : window_(window) {}

  ~SdlWindow() override {
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  void poll(InputState& input) override {
    input.toggle_pause = false;
    input.screenshot = false;
    input.toggle_camera_mode = false;
    input.resized = false;
    input.mouse_dx = 0.0f;
    input.mouse_dy = 0.0f;
    float wheel = 0.0f;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_EVENT_QUIT:
          input.quit = true;
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          input.resized = true;
          break;
        case SDL_EVENT_KEY_DOWN:
          if (e.key.repeat) break;
          if (e.key.key == SDLK_ESCAPE) input.quit = true;
          if (e.key.key == SDLK_P) input.toggle_pause = true;
          if (e.key.key == SDLK_F12) input.screenshot = true;
          if (e.key.key == SDLK_C) input.toggle_camera_mode = true;
          break;
        case SDL_EVENT_MOUSE_MOTION:
          if (e.motion.state & SDL_BUTTON_RMASK) {
            input.mouse_dx += e.motion.xrel;
            input.mouse_dy += e.motion.yrel;
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          wheel -= e.wheel.y;  // scrolling up brings the camera closer
          break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (!gamepad_) {
            gamepad_ = SDL_OpenGamepad(e.gdevice.which);
            if (gamepad_) log_info("gamepad connected: %s", SDL_GetGamepadName(gamepad_));
          }
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if (gamepad_ && SDL_GetGamepadID(gamepad_) == e.gdevice.which) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
            log_info("gamepad disconnected");
          }
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) input.toggle_pause = true;
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) input.quit = true;
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) input.toggle_camera_mode = true;
          break;
        default:
          break;
      }
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    float mx = 0, my = 0, lx = 0, ly = 0, zoom = 0, scrub = 0;
    if (keys[SDL_SCANCODE_D]) mx += 1.0f;
    if (keys[SDL_SCANCODE_A]) mx -= 1.0f;
    if (keys[SDL_SCANCODE_W]) my += 1.0f;
    if (keys[SDL_SCANCODE_S]) my -= 1.0f;
    if (keys[SDL_SCANCODE_RIGHT]) lx += 1.0f;
    if (keys[SDL_SCANCODE_LEFT]) lx -= 1.0f;
    if (keys[SDL_SCANCODE_UP]) ly += 1.0f;
    if (keys[SDL_SCANCODE_DOWN]) ly -= 1.0f;
    if (keys[SDL_SCANCODE_PAGEDOWN]) zoom += 1.0f;
    if (keys[SDL_SCANCODE_PAGEUP]) zoom -= 1.0f;
    if (keys[SDL_SCANCODE_Q]) scrub -= 1.0f;
    if (keys[SDL_SCANCODE_E]) scrub += 1.0f;
    bool run = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];

    if (gamepad_) {
      mx += axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
      my -= axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
      lx += axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX);
      ly -= axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY);
      if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) zoom += 1.0f;
      if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP)) zoom -= 1.0f;
      run = run || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
      scrub += axis_value(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) -
               axis_value(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    }
    // Keep diagonal movement from being faster than straight movement.
    float len = std::sqrt(mx * mx + my * my);
    if (len > 1.0f) {
      mx /= len;
      my /= len;
    }
    input.move_x = mx;
    input.move_y = my;
    input.run = run;
    input.look_x = clampf(lx, -1.0f, 1.0f);
    input.look_y = clampf(ly, -1.0f, 1.0f);
    input.zoom = clampf(zoom, -1.0f, 1.0f) + wheel * 8.0f;  // one wheel notch ~ a short push
    input.time_scrub = clampf(scrub, -1.0f, 1.0f);
  }

  void drawable_size(u32& width, u32& height) const override {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    width = static_cast<u32>(w < 0 ? 0 : w);
    height = static_cast<u32>(h < 0 ? 0 : h);
  }

  std::vector<const char*> vulkan_instance_extensions() const override {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    std::vector<const char*> result;
    for (Uint32 i = 0; i < count; ++i) result.push_back(names[i]);
    return result;
  }

  bool create_vulkan_surface(void* instance, u64* out_surface) override {
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(instance), nullptr, &surface)) {
      log_error("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
      return false;
    }
    *out_surface = reinterpret_cast<u64>(surface);
    return true;
  }

 private:
  SDL_Window* window_ = nullptr;
  SDL_Gamepad* gamepad_ = nullptr;
};

}  // namespace

std::unique_ptr<Window> Window::create(const WindowDesc& desc) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    log_error("SDL_Init failed: %s", SDL_GetError());
    return nullptr;
  }
  SDL_Window* window = SDL_CreateWindow(
      desc.title.c_str(), static_cast<int>(desc.width), static_cast<int>(desc.height),
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) {
    log_error("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return nullptr;
  }
  return std::make_unique<SdlWindow>(window);
}

std::string executable_dir() {
  const char* base = SDL_GetBasePath();  // owned by SDL, do not free
  return base ? std::string(base) : std::string("./");
}

f64 time_seconds() {
  return static_cast<f64>(SDL_GetPerformanceCounter()) /
         static_cast<f64>(SDL_GetPerformanceFrequency());
}

}  // namespace rk
