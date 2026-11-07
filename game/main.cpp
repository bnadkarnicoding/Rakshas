// game/main.cpp — Rung 3: walk a character across a gray-box landscape loaded
// from a content file, under a sky with sun and moon shadows, a moon and stars.
//
// Windowed controls:
//   WASD              (left stick)    ... walk
//   Shift             (left bumper)   ... run
//   Arrow keys        (right stick)   ... look around (or drag with the right mouse button)
//   Mouse wheel / PageUp/PageDown (D-pad up/down) ... camera closer / further
//   Q/E               (triggers)      ... scrub the time of day
//   P                 (Y / Triangle)  ... pause / resume the sun
//   F12                               ... save a screenshot next to the executable
//   Esc               (Back button)   ... quit
//
// Headless (used by automated tests and the cloud workspace):
//   rakshas --headless --validate --time 0.3 --out shot.png
//   rakshas --headless --walk 3 --move "0 1" --run --out walked.png
// Other options: --region <file.rk>  --low (low graphics preset)  --yaw/--pitch <deg>
//                --distance <m>  --width/--height <px>  --day-length <seconds>
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
#include "character.h"
#include "region.h"
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
  float yaw_degrees = std::nanf("");    // NaN = use the region's camera start
  float pitch_degrees = std::nanf("");
  float distance = std::nanf("");
  bool low_preset = false;
  float walk_seconds = 0.0f;   // headless: walk for this long before the picture
  Vec2 move{0.0f, 1.0f};       // headless: stick direction while walking (x right, y forward)
  bool run = false;
  std::string region;
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
    else if (a == "--pitch" && (v = next("--pitch"))) o.pitch_degrees = std::strtof(v, nullptr);
    else if (a == "--region" && (v = next("--region"))) o.region = v;
    else if (a == "--low") o.low_preset = true;
    else if (a == "--walk" && (v = next("--walk"))) o.walk_seconds = std::strtof(v, nullptr);
    else if (a == "--move" && (v = next("--move"))) {
      char* end = nullptr;
      o.move.x = std::strtof(v, &end);
      o.move.y = std::strtof(end, nullptr);
    }
    else if (a == "--run") o.run = true;
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

// Shadow coverage: a square this many metres wide around the view target.
constexpr float kShadowRadius = 60.0f;

// Light-space matrix for sun/moon shadows, snapped to whole shadow-map texels
// so shadow edges don't shimmer as the camera moves.
Mat4 light_matrix(Vec3 light_dir, Vec3 center, u32 shadow_size) {
  Vec3 up = std::fabs(light_dir.y) > 0.98f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  Mat4 view = look_at(center + light_dir * 150.0f, center, up);
  Mat4 proj = orthographic(-kShadowRadius, kShadowRadius, -kShadowRadius, kShadowRadius, 1.0f, 320.0f);
  Mat4 vp = proj * view;
  Vec4 origin = vp * Vec4{0, 0, 0, 1};
  float half = static_cast<float>(shadow_size) * 0.5f;
  float dx = (std::round(origin.x * half) - origin.x * half) / half;
  float dy = (std::round(origin.y * half) - origin.y * half) / half;
  return translate({dx, dy, 0.0f}) * vp;
}

SceneConstants build_scene_constants(const Camera& cam, float aspect, float time_of_day,
                                     u32 shadow_size) {
  SkyState sky = evaluate_sky(time_of_day);
  Mat4 view = look_at(cam.eye(), cam.target, Vec3{0, 1, 0});
  Mat4 proj = perspective(radians(55.0f), aspect, 0.1f, 600.0f);
  SceneConstants sc;
  sc.view_proj = proj * view;
  sc.inv_view_proj = inverse(sc.view_proj);
  sc.light_view_proj = light_matrix(sky.light_dir, cam.target, shadow_size);
  sc.camera_pos = to_vec4(cam.eye(), 1.0f);
  sc.light_dir = to_vec4(sky.light_dir);
  sc.light_color = to_vec4(sky.light_color);
  sc.sun_dir = to_vec4(sky.sun_dir);
  sc.moon_dir = to_vec4(sky.moon_dir, sky.night);
  sc.sky_zenith = to_vec4(sky.sky_zenith);
  sc.sky_horizon = to_vec4(sky.sky_horizon);
  sc.ground_color = to_vec4(sky.ground_bounce);
  sc.params = Vec4{0.006f, 0.9f, 0.0f, 0.0f};
  // Shadows soften as the light gets low and weak (dawn, dusk, moonlight).
  float strength = clampf(0.35f + 0.65f * smoothstepf(0.0f, 0.15f, sky.light_dir.y), 0.0f, 1.0f);
  sc.shadow_params = Vec4{1.0f / static_cast<float>(shadow_size),
                          4.0f * kShadowRadius / static_cast<float>(shadow_size), strength, 0.0f};
  return sc;
}

struct Pipelines {
  rhi::PipelineHandle sky, lit, shadow;
};

void draw_props(rhi::Device& device, const std::vector<Prop>& props) {
  for (const Prop& p : props) {
    ObjectConstants oc{p.model, to_vec4(p.color, 1.0f)};
    device.push_constants(&oc, sizeof(oc));
    device.bind_vertex_buffer(p.mesh->vertices);
    device.bind_index_buffer(p.mesh->indices);
    device.draw_indexed(p.mesh->index_count);
  }
}

void render_frame(rhi::Device& device, const Pipelines& pl, const std::vector<Prop>& props,
                  const Camera& cam, float time_of_day) {
  u32 w = 0, h = 0;
  device.render_size(w, h);
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  SceneConstants sc = build_scene_constants(cam, aspect, time_of_day, device.shadow_map_size());
  device.set_scene_constants(&sc, sizeof(sc));

  device.begin_shadow_pass();
  device.bind_pipeline(pl.shadow);
  draw_props(device, props);
  device.end_pass();

  const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  device.begin_pass(clear);
  device.bind_pipeline(pl.sky);
  device.draw(3);
  device.bind_pipeline(pl.lit);
  draw_props(device, props);
  device.end_pass();
}

constexpr float kCameraHeight = 1.5f;  // camera looks at the player's shoulders
constexpr Vec3 kPlayerColor{0.62f, 0.33f, 0.22f};
constexpr Vec3 kPlayerMarkerColor{0.85f, 0.78f, 0.62f};

// Turns stick/WASD input (x right, y forward) into a world direction, relative
// to where the camera is looking.
Vec2 camera_relative(const Camera& cam, Vec2 input) {
  Vec2 forward{-std::sin(cam.yaw), -std::cos(cam.yaw)};
  Vec2 right{std::cos(cam.yaw), -std::sin(cam.yaw)};
  return forward * input.y + right * input.x;
}

void keep_camera_above_ground(Camera& cam, const Terrain& terrain) {
  Vec3 eye = cam.eye();
  float min_pitch = std::asin(
      clampf((terrain.height_at(eye.x, eye.z) + 0.4f - cam.target.y) / cam.distance, -1.0f, 1.0f));
  if (cam.pitch < min_pitch) cam.pitch = min_pitch;
}

// The camera glides after the player rather than being rigidly attached.
void follow_camera(Camera& cam, const Character& player, const Terrain& terrain, float dt) {
  Vec3 want = player.position + Vec3{0, kCameraHeight, 0};
  cam.target = lerp(cam.target, want, 1.0f - std::exp(-10.0f * dt));
  keep_camera_above_ground(cam, terrain);
}

bool load_pipeline(rhi::Device& device, const std::string& shader_dir, const char* name,
                   bool with_vertices, rhi::PipelineHandle& out) {
  rhi::PipelineDesc d;
  d.debug_name = name;
  const bool shadow = std::strcmp(name, "shadow") == 0;
  if (!read_spirv(shader_dir + name + ".vert.spv", d.vertex_spirv)) return false;
  if (!shadow && !read_spirv(shader_dir + name + ".frag.spv", d.fragment_spirv)) return false;
  if (shadow) {
    d.pass = rhi::PassKind::Shadow;
    d.vertex_stride = sizeof(Vertex);
    d.attributes = {{0, rhi::AttribFormat::Float3, 0}};
    d.cull_back_faces = false;
  } else if (with_vertices) {
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
  desc.shadow_map_size = opt.low_preset ? 1024 : 2048;
  if (!opt.headless) {
    window = Window::create({"Rakshas — rung 3", opt.width, opt.height});
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
  Pipelines pl;
  if (!load_pipeline(*device, shader_dir, "sky", false, pl.sky)) return 1;
  if (!load_pipeline(*device, shader_dir, "lit", true, pl.lit)) return 1;
  if (!load_pipeline(*device, shader_dir, "shadow", true, pl.shadow)) return 1;

  // The land and its placeholder objects come from a content file.
  const std::string region_path =
      opt.region.empty() ? executable_dir() + "content/regions/rung2_forest_lake.rk" : opt.region;
  Region region;
  data::Errors content_errors;
  if (!load_region(region_path, region, content_errors)) {
    log_error("could not load the region:\n%s", content_errors.joined().c_str());
    return 1;
  }
  log_info("region %s: %zu props", region_path.c_str(), region.props.size());

  GpuMesh box = upload(*device, make_box());
  GpuMesh terrain = upload(*device, build_terrain_mesh(region.terrain));
  CharacterParams body;
  GpuMesh capsule = upload(*device, make_capsule(body.radius, body.height));

  std::vector<Prop> scenery = {{&terrain, Mat4::identity(), region.terrain.color}};
  std::vector<Obstacle> obstacles;
  for (const PropPlacement& p : region.props) {
    scenery.push_back({&box, prop_transform(region.terrain, p), p.color});
    obstacles.push_back(obstacle_from_prop(p));
  }

  Character player;
  player.position = {region.player.position.x,
                     region.terrain.height_at(region.player.position.x, region.player.position.y),
                     region.player.position.y};
  player.facing = radians(region.player.facing);

  Camera cam;
  cam.yaw = radians(region.camera.yaw + region.player.facing);
  // In headless runs, --yaw sets the picture's view after any scripted walk
  // (so it doesn't change which way the walk goes).
  if (!std::isnan(opt.yaw_degrees) && opt.walk_seconds <= 0.0f) cam.yaw = radians(opt.yaw_degrees);
  cam.pitch = radians(std::isnan(opt.pitch_degrees) ? region.camera.pitch : opt.pitch_degrees);
  cam.distance = std::isnan(opt.distance) ? region.camera.distance : opt.distance;
  cam.target = player.position + Vec3{0, kCameraHeight, 0};
  float time_of_day = opt.time_of_day;
  int exit_code = 0;

  // The world is drawn from the static scenery plus the player's body.
  auto frame_props = [&]() {
    std::vector<Prop> all = scenery;
    Mat4 at = translate(player.position) * rotate_y(player.facing);
    all.push_back({&capsule, at, kPlayerColor});
    // A small marker on the face shows which way the body is facing.
    all.push_back({&box, at * translate({0.0f, body.height - 0.28f, -body.radius}) *
                             scale({0.16f, 0.1f, 0.14f}),
                   kPlayerMarkerColor});
    return all;
  };

  // Fixed-rate simulation (ARCHITECTURE.md section 4).
  const float sim_dt = 1.0f / 60.0f;
  auto simulate = [&](Vec2 move_input, bool run) {
    MoveIntent intent{camera_relative(cam, move_input), run};
    step_character(player, body, region.terrain, obstacles, intent, sim_dt);
  };

  if (opt.headless) {
    // Optional scripted walk, e.g. --walk 3 --move "0 1" --run
    int steps = static_cast<int>(opt.walk_seconds / sim_dt);
    for (int i = 0; i < steps; ++i) {
      simulate(opt.move, opt.run);
      follow_camera(cam, player, region.terrain, sim_dt);
    }
    if (steps > 0) {
      log_info("walked %.1f s, now at (%.2f, %.2f, %.2f)", static_cast<double>(opt.walk_seconds),
               static_cast<double>(player.position.x), static_cast<double>(player.position.y),
               static_cast<double>(player.position.z));
    }
    if (!std::isnan(opt.yaw_degrees)) cam.yaw = radians(opt.yaw_degrees);
    cam.target = player.position + Vec3{0, kCameraHeight, 0};
    keep_camera_above_ground(cam, region.terrain);
    std::vector<Prop> props = frame_props();
    for (u32 i = 0; i < opt.frames; ++i) {
      if (!device->begin_frame()) continue;
      render_frame(*device, pl, props, cam, time_of_day);
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
    log_info("controls: WASD move, Shift run, arrows or right-mouse drag look, wheel zoom,");
    log_info("          Q/E time of day, P pause sun, F12 screenshot, Esc quit");
    InputState input;
    bool paused = false;
    int shot = 0;
    int last_reported_hour = -1;
    float accumulator = 0.0f;
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

      // Look around: arrows / right stick / right-mouse drag. Turning right is -yaw.
      cam.yaw -= input.look_x * dt * 2.2f + input.mouse_dx * 0.004f;
      cam.pitch = clampf(cam.pitch - input.look_y * dt * 1.2f + input.mouse_dy * 0.004f,
                         radians(-25.0f), radians(75.0f));
      cam.distance = clampf(cam.distance * std::exp(input.zoom * dt * 1.2f), 2.5f, 30.0f);

      // Simulation runs in fixed steps, however fast or slow frames are.
      accumulator += dt;
      while (accumulator >= sim_dt) {
        simulate({input.move_x, input.move_y}, input.run);
        accumulator -= sim_dt;
      }
      follow_camera(cam, player, region.terrain, dt);

      if (!device->begin_frame()) continue;
      render_frame(*device, pl, frame_props(), cam, time_of_day);
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
