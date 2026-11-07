// game/main.cpp — Rung 1: a window, a sun-lit gray-box scene under a natural
// sky, and an orbiting view driven by keyboard or gamepad.
//
// Windowed controls:
//   A/D or Left/Right (left stick X) ... orbit the view
//   W/S or Up/Down    (left stick Y) ... move closer / further
//   Q/E               (triggers)     ... scrub the time of day
//   Space             (South button) ... pause / resume the sun
//   F12                              ... save a screenshot next to the executable
//   Esc               (Back button)  ... quit
//
// Headless (used by automated tests and the cloud workspace):
//   rakshas --headless --validate --time 0.3 --yaw 35 --out shot.png
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "core/file.h"
#include "core/log.h"
#include "core/math.h"
#include "platform/platform.h"
#include "render/rhi.h"
#include "scene_constants.h"
#include "screenshot.h"
#include "shapes.h"
#include "sky_model.h"

using namespace rk;

namespace {

struct Options {
  bool headless = false;
  bool validate = false;
  u32 width = 1280;
  u32 height = 720;
  u32 frames = 3;
  float time_of_day = 0.30f;
  float yaw_degrees = 35.0f;
  float distance = 11.0f;
  float day_length_seconds = 180.0f;
  std::string out = "rakshas_shot.png";
};

bool parse_args(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        log_error("missing value for %s", name);
        return nullptr;
      }
      return argv[++i];
    };
    const char* v = nullptr;
    if (a == "--headless") o.headless = true;
    else if (a == "--validate") o.validate = true;
    else if (a == "--width" && (v = next("--width"))) o.width = static_cast<u32>(std::atoi(v));
    else if (a == "--height" && (v = next("--height"))) o.height = static_cast<u32>(std::atoi(v));
    else if (a == "--frames" && (v = next("--frames"))) o.frames = static_cast<u32>(std::atoi(v));
    else if (a == "--time" && (v = next("--time"))) o.time_of_day = std::strtof(v, nullptr);
    else if (a == "--yaw" && (v = next("--yaw"))) o.yaw_degrees = std::strtof(v, nullptr);
    else if (a == "--distance" && (v = next("--distance"))) o.distance = std::strtof(v, nullptr);
    else if (a == "--day-length" && (v = next("--day-length"))) o.day_length_seconds = std::strtof(v, nullptr);
    else if (a == "--out" && (v = next("--out"))) o.out = v;
    else {
      log_error("unknown or incomplete argument: %s", a.c_str());
      return false;
    }
  }
  if (o.width == 0 || o.height == 0 || o.frames == 0) {
    log_error("width, height and frames must be positive");
    return false;
  }
  return true;
}

float wrap01(float t) { return t - std::floor(t); }

// "06:45"-style text for a time of day in 0..1.
std::string clock_text(float time_of_day) {
  int minutes = static_cast<int>(wrap01(time_of_day) * 24.0f * 60.0f) % (24 * 60);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

struct GpuMesh {
  rhi::BufferHandle vertices;
  rhi::BufferHandle indices;
  u32 index_count = 0;
};

GpuMesh upload(rhi::Device& device, const Mesh& mesh) {
  GpuMesh g;
  g.vertices = device.create_buffer(rhi::BufferUsage::Vertex, mesh.vertices.data(),
                                    mesh.vertices.size() * sizeof(Vertex));
  g.indices = device.create_buffer(rhi::BufferUsage::Index32, mesh.indices.data(),
                                   mesh.indices.size() * sizeof(u32));
  g.index_count = static_cast<u32>(mesh.indices.size());
  return g;
}

// A gray-box object: which mesh, where, and what colour (linear).
struct Prop {
  const GpuMesh* mesh;
  Mat4 model;
  Vec3 color;
};

Mat4 place(Vec3 position, Vec3 size, float yaw_degrees = 0.0f) {
  return translate(position) * rotate_y(radians(yaw_degrees)) * scale(size);
}

struct Camera {
  Vec3 target{0.0f, 1.0f, 0.0f};
  float yaw = 0.0f;       // radians
  float pitch = 0.30f;    // radians above the horizon
  float distance = 11.0f;

  Vec3 eye() const {
    float c = std::cos(pitch);
    return target + Vec3{std::sin(yaw) * c, std::sin(pitch), std::cos(yaw) * c} * distance;
  }
};

SceneConstants build_scene_constants(const Camera& cam, float aspect, float time_of_day) {
  SkyState sky = evaluate_sky(time_of_day);
  Mat4 view = look_at(cam.eye(), cam.target, Vec3{0, 1, 0});
  Mat4 proj = perspective(radians(55.0f), aspect, 0.1f, 500.0f);
  SceneConstants sc;
  sc.view_proj = proj * view;
  sc.inv_view_proj = inverse(sc.view_proj);
  sc.camera_pos = to_vec4(cam.eye(), 1.0f);
  sc.sun_dir = to_vec4(sky.light_dir);
  sc.sun_color = to_vec4(sky.light_color);
  sc.sky_zenith = to_vec4(sky.sky_zenith);
  sc.sky_horizon = to_vec4(sky.sky_horizon);
  sc.ground_color = to_vec4(sky.ground_bounce);
  sc.params = Vec4{0.006f, 0.9f, 0.0f, 0.0f};
  return sc;
}

void render_frame(rhi::Device& device, rhi::PipelineHandle sky, rhi::PipelineHandle lit,
                  const std::vector<Prop>& props, const Camera& cam, float time_of_day) {
  u32 w = 0, h = 0;
  device.render_size(w, h);
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  SceneConstants sc = build_scene_constants(cam, aspect, time_of_day);
  device.set_scene_constants(&sc, sizeof(sc));

  const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  device.begin_pass(clear);
  device.bind_pipeline(sky);
  device.draw(3);
  device.bind_pipeline(lit);
  for (const Prop& p : props) {
    ObjectConstants oc{p.model, to_vec4(p.color, 1.0f)};
    device.push_constants(&oc, sizeof(oc));
    device.bind_vertex_buffer(p.mesh->vertices);
    device.bind_index_buffer(p.mesh->indices);
    device.draw_indexed(p.mesh->index_count);
  }
  device.end_pass();
}

bool load_pipeline(rhi::Device& device, const std::string& shader_dir, const char* name,
                   bool with_vertices, rhi::PipelineHandle& out) {
  rhi::PipelineDesc d;
  d.debug_name = name;
  if (!read_spirv(shader_dir + name + ".vert.spv", d.vertex_spirv)) return false;
  if (!read_spirv(shader_dir + name + ".frag.spv", d.fragment_spirv)) return false;
  if (with_vertices) {
    d.vertex_stride = sizeof(Vertex);
    d.attributes = {{0, rhi::AttribFormat::Float3, 0},
                    {1, rhi::AttribFormat::Float3, static_cast<u32>(sizeof(Vec3))}};
  } else {
    d.depth_test = false;
    d.depth_write = false;
    d.cull_back_faces = false;
  }
  out = device.create_pipeline(d);
  return out.valid();
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, opt)) return 1;

  std::unique_ptr<Window> window;
  rhi::DeviceDesc desc;
  desc.validation = opt.validate;
  desc.headless = opt.headless;
  desc.width = opt.width;
  desc.height = opt.height;
  if (!opt.headless) {
    window = Window::create({"Rakshas — rung 1", opt.width, opt.height});
    if (!window) return 1;
    desc.instance_extensions = window->vulkan_instance_extensions();
    desc.create_surface = [&](void* instance, u64* surface) {
      return window->create_vulkan_surface(instance, surface);
    };
    desc.drawable_size = [&](u32& w, u32& h) { window->drawable_size(w, h); };
  }

  auto device = rhi::create_vulkan_device(desc);
  if (!device) return 1;

  const std::string shader_dir = executable_dir() + "shaders/";
  rhi::PipelineHandle sky_pipeline, lit_pipeline;
  if (!load_pipeline(*device, shader_dir, "sky", false, sky_pipeline)) return 1;
  if (!load_pipeline(*device, shader_dir, "lit", true, lit_pipeline)) return 1;

  GpuMesh box = upload(*device, make_box());
  GpuMesh ground = upload(*device, make_ground(400.0f));

  // Rung 1 gray-box scene: a stone block, a sandstone pillar, a low step and a
  // distant row of pillars to show haze and scale.
  std::vector<Prop> props = {
      {&ground, Mat4::identity(), {0.42f, 0.34f, 0.24f}},
      {&box, place({0.0f, 0.75f, 0.0f}, {1.5f, 1.5f, 1.5f}, 15.0f), {0.55f, 0.50f, 0.45f}},
      {&box, place({-2.6f, 1.75f, -1.6f}, {0.7f, 3.5f, 0.7f}, 20.0f), {0.72f, 0.52f, 0.36f}},
      {&box, place({2.4f, 0.25f, -0.9f}, {2.6f, 0.5f, 1.6f}, -10.0f), {0.50f, 0.42f, 0.33f}},
  };
  for (int i = -3; i <= 3; ++i) {
    float x = static_cast<float>(i) * 6.0f;
    props.push_back({&box, place({x, 2.5f, -24.0f}, {1.0f, 5.0f, 1.0f}), {0.70f, 0.55f, 0.40f}});
  }

  Camera cam;
  cam.yaw = radians(opt.yaw_degrees);
  cam.distance = opt.distance;
  float time_of_day = opt.time_of_day;
  int exit_code = 0;

  if (opt.headless) {
    for (u32 i = 0; i < opt.frames; ++i) {
      if (!device->begin_frame()) continue;
      render_frame(*device, sky_pipeline, lit_pipeline, props, cam, time_of_day);
      device->end_frame();
    }
    std::vector<u8> pixels;
    u32 w = 0, h = 0;
    if (!device->capture_last_frame(pixels, w, h) || !write_png(opt.out, pixels, w, h)) {
      exit_code = 1;
    } else {
      log_info("wrote %s (%ux%u) on %s", opt.out.c_str(), w, h, device->device_name());
    }
  } else {
    log_info("controls: A/D orbit, W/S zoom, Q/E time of day, Space pause sun, F12 screenshot, Esc quit");
    InputState input;
    bool paused = false;
    int shot = 0;
    int last_reported_hour = -1;
    log_info("time of day %s, a full day lasts %.0f seconds", clock_text(time_of_day).c_str(),
             static_cast<double>(opt.day_length_seconds));
    f64 last = time_seconds();
    while (!input.quit) {
      window->poll(input);
      f64 now = time_seconds();
      float dt = static_cast<float>(now - last);
      last = now;
      if (dt > 0.1f) dt = 0.1f;

      if (input.toggle_pause) {
        paused = !paused;
        log_info("sun %s at %s", paused ? "paused" : "moving again", clock_text(time_of_day).c_str());
      }
      if (!paused) time_of_day += dt / opt.day_length_seconds;
      // Holding Q/E sweeps a full day in about four seconds.
      time_of_day += input.time_scrub * dt * 0.25f;
      if (input.time_scrub != 0.0f) {
        int hour = static_cast<int>(std::floor(wrap01(time_of_day) * 24.0f));
        if (hour != last_reported_hour) {
          last_reported_hour = hour;
          log_info("time of day %s", clock_text(time_of_day).c_str());
        }
      }
      cam.yaw += input.orbit * dt * 1.2f;
      cam.distance = clampf(cam.distance * std::exp(input.zoom * dt * 1.2f), 4.0f, 40.0f);

      if (!device->begin_frame()) continue;
      render_frame(*device, sky_pipeline, lit_pipeline, props, cam, time_of_day);
      device->end_frame();

      if (input.screenshot) {
        std::vector<u8> pixels;
        u32 w = 0, h = 0;
        std::string path = executable_dir() + "screenshot_" + std::to_string(shot++) + ".png";
        if (device->capture_last_frame(pixels, w, h) && write_png(path, pixels, w, h))
          log_info("saved %s", path.c_str());
      }
    }
  }

  device->wait_idle();
  u32 validation_errors = device->validation_error_count();
  device.reset();
  window.reset();
  if (validation_errors > 0) {
    log_error("%u Vulkan validation error(s) reported", validation_errors);
    return 2;
  }
  if (exit_code == 0 && log_error_count() > 0) return 2;
  return exit_code;
}
