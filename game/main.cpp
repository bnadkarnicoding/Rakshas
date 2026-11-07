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
#include <deque>
#include <map>
#include <memory>
#include <string>

#include "core/file.h"
#include "core/log.h"
#include "core/math.h"
#include "platform/platform.h"
#include "render/rhi.h"
#include "scene_constants.h"
#include "screenshot.h"
#include "camera_director.h"
#include "foliage.h"
#include "model.h"
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
  float settle_seconds = 2.5f; // headless: camera settling time after a walk
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

// Something drawn with the lit pipeline: which mesh, where, its colour (linear)
// and texture. World-mapped textures are projected from the world's axes, so
// terrain and blocks of any size are never stretched.
struct Prop {
  const GpuMesh* mesh;
  Mat4 model;
  Vec3 color;
  rhi::TextureHandle texture;
  float world_mapped_metres = 0.0f;  // > 0: the texture repeats every N metres
  bool casts_shadow = true;
};

// Many copies of one mesh (trees, grass) drawn in a single call.
struct FoliageGroup {
  const GpuMesh* mesh;
  rhi::BufferHandle instances;
  u32 count = 0;
  Vec3 color;
  float wind = 0.0f;    // metres of sway at the top
  float height = 1.0f;  // mesh height, so sway grows toward the top
  bool casts_shadow = true;
};

struct Scene {
  std::vector<Prop> props;
  std::vector<FoliageGroup> foliage;
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

SceneConstants build_scene_constants(const CameraRig& cam, float aspect, float time_of_day,
                                     u32 shadow_size, float seconds) {
  SkyState sky = evaluate_sky(time_of_day);
  Mat4 view = look_at(cam.eye(), cam.look_at_point(), Vec3{0, 1, 0});
  Mat4 proj = perspective(cam.fov, aspect, 0.1f, 600.0f);
  SceneConstants sc;
  sc.view_proj = proj * view;
  sc.inv_view_proj = inverse(sc.view_proj);
  sc.light_view_proj = light_matrix(sky.light_dir, cam.focus, shadow_size);
  sc.camera_pos = to_vec4(cam.eye(), 1.0f);
  sc.light_dir = to_vec4(sky.light_dir);
  sc.light_color = to_vec4(sky.light_color);
  sc.sun_dir = to_vec4(sky.sun_dir);
  sc.moon_dir = to_vec4(sky.moon_dir, sky.night);
  sc.sky_zenith = to_vec4(sky.sky_zenith);
  sc.sky_horizon = to_vec4(sky.sky_horizon);
  sc.ground_color = to_vec4(sky.ground_bounce);
  sc.params = Vec4{0.006f, 0.9f, seconds, 0.0f};
  // Shadows soften as the light gets low and weak (dawn, dusk, moonlight).
  float strength = clampf(0.35f + 0.65f * smoothstepf(0.0f, 0.15f, sky.light_dir.y), 0.0f, 1.0f);
  sc.shadow_params = Vec4{1.0f / static_cast<float>(shadow_size),
                          4.0f * kShadowRadius / static_cast<float>(shadow_size), strength, 0.0f};
  return sc;
}

struct Pipelines {
  rhi::PipelineHandle sky, lit, shadow, foliage, shadow_foliage;
};

void draw_props(rhi::Device& device, const std::vector<Prop>& props, bool shadow_pass) {
  for (const Prop& p : props) {
    if (shadow_pass && !p.casts_shadow) continue;
    ObjectConstants oc{p.model, to_vec4(p.color, 1.0f),
                       {p.world_mapped_metres > 0.0f ? 1.0f : 0.0f, p.world_mapped_metres, 0, 0}};
    device.push_constants(&oc, sizeof(oc));
    if (!shadow_pass) device.bind_texture(p.texture);
    device.bind_vertex_buffer(p.mesh->vertices);
    device.bind_index_buffer(p.mesh->indices);
    device.draw_indexed(p.mesh->index_count);
  }
}

void draw_foliage(rhi::Device& device, const std::vector<FoliageGroup>& groups, bool shadow_pass) {
  for (const FoliageGroup& g : groups) {
    if (g.count == 0 || (shadow_pass && !g.casts_shadow)) continue;
    ObjectConstants oc{Mat4::identity(), to_vec4(g.color, 1.0f), {0, 0, g.wind, g.height}};
    device.push_constants(&oc, sizeof(oc));
    device.bind_vertex_buffer(g.mesh->vertices);
    device.bind_instance_buffer(g.instances);
    device.bind_index_buffer(g.mesh->indices);
    device.draw_indexed_instanced(g.mesh->index_count, g.count);
  }
}

void render_frame(rhi::Device& device, const Pipelines& pl, const Scene& scene, const CameraRig& cam,
                  float time_of_day, float seconds) {
  u32 w = 0, h = 0;
  device.render_size(w, h);
  float aspect = static_cast<float>(w) / static_cast<float>(h);
  SceneConstants sc =
      build_scene_constants(cam, aspect, time_of_day, device.shadow_map_size(), seconds);
  device.set_scene_constants(&sc, sizeof(sc));

  device.begin_shadow_pass();
  device.bind_pipeline(pl.shadow);
  draw_props(device, scene.props, true);
  device.bind_pipeline(pl.shadow_foliage);
  draw_foliage(device, scene.foliage, true);
  device.end_pass();

  const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  device.begin_pass(clear);
  device.bind_pipeline(pl.sky);
  device.draw(3);
  device.bind_pipeline(pl.lit);
  draw_props(device, scene.props, false);
  device.bind_pipeline(pl.foliage);
  draw_foliage(device, scene.foliage, false);
  device.end_pass();
}

constexpr Vec3 kPlayerColor{0.62f, 0.33f, 0.22f};
constexpr Vec3 kPlayerMarkerColor{0.85f, 0.78f, 0.62f};
constexpr Vec3 kBarkColor{0.36f, 0.27f, 0.20f};
constexpr Vec3 kLeafColor{0.16f, 0.26f, 0.10f};
constexpr Vec3 kGrassColor{0.36f, 0.38f, 0.17f};

// Turns stick/WASD input (x right, y forward) into a world direction, relative
// to where the camera is looking.
Vec2 camera_relative(float camera_yaw, Vec2 input) {
  Vec2 forward{-std::sin(camera_yaw), -std::cos(camera_yaw)};
  Vec2 right{std::cos(camera_yaw), -std::sin(camera_yaw)};
  return forward * input.y + right * input.x;
}

// Model matrix for a placed prop: boxes rest on the ground (see prop_transform);
// figures and models stand upright on the ground at their position.
Mat4 placed_transform(const Terrain& terrain, const PropPlacement& p, const CharacterParams& body) {
  Vec3 feet{p.position.x, terrain.height_at(p.position.x, p.position.y), p.position.y};
  if (p.shape == PropShape::Figure) {
    return translate(feet) * rotate_y(radians(p.yaw)) *
           scale({p.size.x / (2.0f * body.radius), p.size.y / body.height, p.size.z / (2.0f * body.radius)});
  }
  if (p.shape == PropShape::Model) return translate(feet - Vec3{0, 0.05f, 0}) * rotate_y(radians(p.yaw));
  return prop_transform(terrain, p);
}

enum class PipelineKind { Sky, Lit, Shadow, Foliage, ShadowFoliage };

bool load_pipeline(rhi::Device& device, const std::string& shader_dir, const char* name,
                   PipelineKind kind, rhi::PipelineHandle& out) {
  rhi::PipelineDesc d;
  d.debug_name = name;
  const bool shadow = kind == PipelineKind::Shadow || kind == PipelineKind::ShadowFoliage;
  if (!read_spirv(shader_dir + name + ".vert.spv", d.vertex_spirv)) return false;
  if (!shadow && !read_spirv(shader_dir + name + ".frag.spv", d.fragment_spirv)) return false;
  const std::vector<rhi::VertexAttribute> mesh_attributes = {
      {0, rhi::AttribFormat::Float3, 0},
      {1, rhi::AttribFormat::Float3, static_cast<u32>(sizeof(Vec3))},
      {2, rhi::AttribFormat::Float2, static_cast<u32>(2 * sizeof(Vec3))}};
  const std::vector<rhi::VertexAttribute> instance_attributes = {
      {3, rhi::AttribFormat::Float4, 0},
      {4, rhi::AttribFormat::Float4, 16},
      {5, rhi::AttribFormat::Float4, 32}};
  switch (kind) {
    case PipelineKind::Sky:
      d.depth_test = false;
      d.depth_write = false;
      d.cull_back_faces = false;
      break;
    case PipelineKind::Lit:
      d.vertex_stride = sizeof(Vertex);
      d.attributes = mesh_attributes;
      break;
    case PipelineKind::Shadow:
      d.pass = rhi::PassKind::Shadow;
      d.vertex_stride = sizeof(Vertex);
      d.attributes = {mesh_attributes[0]};
      d.cull_back_faces = false;
      break;
    case PipelineKind::Foliage:
    case PipelineKind::ShadowFoliage:
      if (shadow) d.pass = rhi::PassKind::Shadow;
      d.vertex_stride = sizeof(Vertex);
      d.attributes = mesh_attributes;
      d.instance_stride = sizeof(FoliageInstance);
      d.instance_attributes = instance_attributes;
      d.cull_back_faces = false;  // grass blades are seen from both sides
      break;
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
    window = Window::create({"Rakshas — detail preview", opt.width, opt.height});
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
  if (!load_pipeline(*device, shader_dir, "sky", PipelineKind::Sky, pl.sky)) return 1;
  if (!load_pipeline(*device, shader_dir, "lit", PipelineKind::Lit, pl.lit)) return 1;
  if (!load_pipeline(*device, shader_dir, "shadow", PipelineKind::Shadow, pl.shadow)) return 1;
  if (!load_pipeline(*device, shader_dir, "foliage", PipelineKind::Foliage, pl.foliage)) return 1;
  if (!load_pipeline(*device, shader_dir, "shadow_foliage", PipelineKind::ShadowFoliage, pl.shadow_foliage))
    return 1;

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

  // Textures are loaded once and shared. A plain white texture stands in
  // wherever no image is given, so the colour shows as-is.
  const std::string content_dir = executable_dir() + "content/";
  const u8 white_pixel[4] = {255, 255, 255, 255};
  rhi::TextureHandle white = device->create_texture({"white", 1, 1, white_pixel, true, false});
  std::map<std::string, rhi::TextureHandle> texture_cache;
  auto texture = [&](const std::string& path) -> rhi::TextureHandle {
    if (path.empty()) return white;
    auto found = texture_cache.find(path);
    if (found != texture_cache.end()) return found->second;
    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    rhi::TextureHandle handle = white;
    if (load_image_rgba(path, rgba, w, h)) {
      handle = device->create_texture({path.c_str(), w, h, rgba.data(), true, true});
    } else {
      log_warn("texture not found or unreadable: %s (using plain colour)", path.c_str());
    }
    texture_cache[path] = handle;
    return handle;
  };
  auto texture_file = [&](const std::string& name) {
    return name.empty() ? std::string() : content_dir + "textures/" + name;
  };

  Scene scene;
  scene.props.push_back({&terrain, Mat4::identity(), region.terrain.color,
                         texture(texture_file(region.terrain.texture)), 4.0f, true});
  std::vector<Obstacle> obstacles;
  std::deque<GpuMesh> model_meshes;  // stable addresses for Prop::mesh
  std::map<std::string, std::vector<std::pair<const GpuMesh*, ModelPart>>> model_cache;
  for (const PropPlacement& p : region.props) {
    Mat4 at = placed_transform(region.terrain, p, body);
    if (p.shape == PropShape::Model) {
      auto it = model_cache.find(p.model);
      if (it == model_cache.end()) {
        Model model;
        data::Errors model_errors;
        if (!load_model(content_dir + p.model, model, model_errors))
          log_warn("model '%s':\n%s", p.model.c_str(), model_errors.joined().c_str());
        std::vector<std::pair<const GpuMesh*, ModelPart>> parts;
        for (ModelPart& part : model.parts) {
          model_meshes.push_back(upload(*device, part.mesh));
          part.mesh = Mesh{};  // geometry now lives on the GPU
          parts.emplace_back(&model_meshes.back(), std::move(part));
        }
        it = model_cache.emplace(p.model, std::move(parts)).first;
      }
      for (const auto& [mesh, part] : it->second) {
        Vec3 c{part.color.x * p.color.x, part.color.y * p.color.y, part.color.z * p.color.z};
        scene.props.push_back({mesh, at, c, texture(part.texture_path), 0.0f, true});
      }
    } else {
      const GpuMesh* mesh = p.shape == PropShape::Figure ? &capsule : &box;
      float mapped = p.shape == PropShape::Box && !p.texture.empty() ? 3.0f : 0.0f;
      scene.props.push_back({mesh, at, p.color, texture(texture_file(p.texture)), mapped, true});
    }
    obstacles.push_back(obstacle_from_prop(p));
  }

  // Trees and grass, placed from the region's forest and grass areas.
  GpuMesh trunk = upload(*device, make_tree_trunk());
  GpuMesh canopy = upload(*device, make_tree_canopy());
  GpuMesh grass = upload(*device, make_grass_clump());
  std::vector<TreePlacement> trees = place_trees(region);
  std::vector<FoliageInstance> tree_data = tree_instances(region, trees);
  std::vector<FoliageInstance> grass_data = place_grass(region, opt.low_preset ? 0.5f : 1.0f);
  for (const Obstacle& o : tree_obstacles(trees)) obstacles.push_back(o);
  if (!tree_data.empty()) {
    rhi::BufferHandle buf = device->create_buffer(rhi::BufferUsage::Instance, tree_data.data(),
                                                  tree_data.size() * sizeof(FoliageInstance));
    u32 n = static_cast<u32>(tree_data.size());
    scene.foliage.push_back({&trunk, buf, n, kBarkColor, 0.03f, kTreeHeight, true});
    scene.foliage.push_back({&canopy, buf, n, kLeafColor, 0.12f, kTreeHeight, true});
  }
  if (!grass_data.empty()) {
    rhi::BufferHandle buf = device->create_buffer(rhi::BufferUsage::Instance, grass_data.data(),
                                                  grass_data.size() * sizeof(FoliageInstance));
    scene.foliage.push_back({&grass, buf, static_cast<u32>(grass_data.size()), kGrassColor, 0.06f,
                             kGrassHeight, false});
  }
  log_info("%zu trees, %zu grass clumps, %zu textures", trees.size(), grass_data.size(),
           texture_cache.size());

  Character player;
  player.position = {region.player.position.x,
                     region.terrain.height_at(region.player.position.x, region.player.position.y),
                     region.player.position.y};
  player.facing = radians(region.player.facing);

  CameraDirector director;
  director.reset(player, radians(region.camera.yaw + region.player.facing));
  log_info("%zu camera zone(s) in this region", region.camera_zones.size());
  float time_of_day = opt.time_of_day;
  int exit_code = 0;

  // The world is drawn from the static scene plus the player's body.
  auto frame_scene = [&]() {
    Scene all = scene;
    Mat4 at = translate(player.position) * rotate_y(player.facing);
    all.props.push_back({&capsule, at, kPlayerColor, white, 0.0f, true});
    // A small marker on the face shows which way the body is facing.
    all.props.push_back({&box, at * translate({0.0f, body.height - 0.28f, -body.radius}) *
                                   scale({0.16f, 0.1f, 0.14f}),
                         kPlayerMarkerColor, white, 0.0f, true});
    return all;
  };

  // Fixed-rate simulation (ARCHITECTURE.md section 4).
  const float sim_dt = 1.0f / 60.0f;
  auto simulate = [&](Vec2 move_input, bool run) {
    MoveIntent intent{camera_relative(director.rig().yaw, move_input), run};
    step_character(player, body, region.terrain, obstacles, intent, sim_dt);
  };

  CameraRig override_rig;
  bool use_override = false;
  if (opt.headless) {
    // Optional scripted walk, e.g. --walk 3 --move "0 1" --run
    // The walk direction is fixed relative to the starting view, so a scripted
    // path doesn't wander when a camera zone turns the camera.
    const float start_yaw = director.rig().yaw;
    int steps = static_cast<int>(opt.walk_seconds / sim_dt);
    for (int i = 0; i < steps; ++i) {
      step_character(player, body, region.terrain, obstacles,
                     MoveIntent{camera_relative(start_yaw, opt.move), opt.run}, sim_dt);
      director.update(player, region.terrain, region.camera_zones, {}, sim_dt);
    }
    // Let the camera settle after walking, as a player standing still would see it.
    int settle = steps > 0 ? static_cast<int>(opt.settle_seconds / sim_dt) : 0;
    for (int i = 0; i < settle; ++i) {
      simulate({0, 0}, false);
      director.update(player, region.terrain, region.camera_zones, {}, sim_dt);
    }
    if (steps > 0) {
      log_info("walked %.1f s, now at (%.2f, %.2f, %.2f)", static_cast<double>(opt.walk_seconds),
               static_cast<double>(player.position.x), static_cast<double>(player.position.y),
               static_cast<double>(player.position.z));
    }
    if (steps > 0) {
      log_info("camera framing: %s%s%s", framing_name(director.framing()),
               director.zone_name().empty() ? "" : " in ", director.zone_name().c_str());
    }
    if (!std::isnan(opt.yaw_degrees) || !std::isnan(opt.pitch_degrees) || !std::isnan(opt.distance)) {
      // Picture-only view override (debugging): starts from the current framing.
      CameraRig rig = director.rig();
      if (!std::isnan(opt.yaw_degrees)) rig.yaw = radians(opt.yaw_degrees);
      if (!std::isnan(opt.pitch_degrees)) rig.pitch = radians(opt.pitch_degrees);
      if (!std::isnan(opt.distance)) rig.distance = opt.distance;
      override_rig = rig;
      use_override = true;
    }
    Scene frame = frame_scene();
    for (u32 i = 0; i < opt.frames; ++i) {
      if (!device->begin_frame()) continue;
      render_frame(*device, pl, frame, use_override ? override_rig : director.rig(), time_of_day,
                   opt.walk_seconds + opt.settle_seconds);
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
    log_info("          C camera zone mode, Q/E time of day, P pause sun, F12 screenshot, Esc quit");
    InputState input;
    bool paused = false;
    int shot = 0;
    int last_reported_hour = -1;
    float accumulator = 0.0f;
    float seconds = 0.0f;
    Framing last_framing = Framing::Explore;
    std::string last_zone;
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

      if (input.toggle_camera_mode) {
        director.directed_only = !director.directed_only;
        log_info("camera zones: %s", director.directed_only
                                         ? "fully directed (look input ignored inside zones)"
                                         : "nudge (look around, view drifts back when you let go)");
      }

      // Simulation runs in fixed steps, however fast or slow frames are.
      accumulator += dt;
      while (accumulator >= sim_dt) {
        simulate({input.move_x, input.move_y}, input.run);
        accumulator -= sim_dt;
      }

      // The camera director: look input (arrows / right stick / right-mouse
      // drag; turning right is -yaw) plus the framing of any zone we're in.
      CameraInput look;
      look.yaw = -(input.look_x * dt * 2.2f + input.mouse_dx * 0.004f);
      look.pitch = -input.look_y * dt * 1.2f + input.mouse_dy * 0.004f;
      look.zoom = input.zoom * dt * 1.2f;
      director.update(player, region.terrain, region.camera_zones, look, dt);
      if (director.framing() != last_framing || director.zone_name() != last_zone) {
        last_framing = director.framing();
        last_zone = director.zone_name();
        log_info("camera: %s%s%s", framing_name(last_framing), last_zone.empty() ? "" : " (",
                 last_zone.empty() ? "" : (last_zone + ")").c_str());
      }

      if (!device->begin_frame()) continue;
      seconds += dt;
      render_frame(*device, pl, frame_scene(), director.rig(), time_of_day, seconds);
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
