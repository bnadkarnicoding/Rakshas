// game/region.cpp — region loading and terrain shaping.
#include "region.h"

#include <algorithm>

namespace rk {
namespace {

// Smooth bump: 1 at the centre, easing to 0 at distance 1.
float bump(float t) {
  if (t >= 1.0f) return 0.0f;
  return 0.5f + 0.5f * std::cos(kPi * t);
}

u32 hash2(int x, int z, int seed) {
  u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(z) * 668265263u +
          static_cast<u32>(seed) * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

float lattice(int x, int z, int seed) {
  return static_cast<float>(hash2(x, z, seed) & 0xFFFFu) / 65535.0f;  // 0..1
}

// Smooth value noise in -1..1.
float value_noise(float x, float z, int seed) {
  float fx = std::floor(x), fz = std::floor(z);
  int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  float tx = x - fx, tz = z - fz;
  tx = tx * tx * (3.0f - 2.0f * tx);
  tz = tz * tz * (3.0f - 2.0f * tz);
  float a = lattice(ix, iz, seed), b = lattice(ix + 1, iz, seed);
  float c = lattice(ix, iz + 1, seed), d = lattice(ix + 1, iz + 1, seed);
  return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), tz) * 2.0f - 1.0f;
}

float distance_to_segment(Vec2 p, Vec2 a, Vec2 b) {
  Vec2 ab = b - a;
  float len2 = dot(ab, ab);
  float t = len2 > 0.0f ? clampf(dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
  return length(p - (a + ab * t));
}

bool read_vec2(const data::Node& n, const char* key, Vec2& out, data::Errors& e, bool required) {
  float v[2] = {out.x, out.y};
  if (!data::read_numbers(n, key, v, 2, e, required)) return false;
  out = {v[0], v[1]};
  return true;
}

bool read_vec3(const data::Node& n, const char* key, Vec3& out, data::Errors& e, bool required) {
  float v[3] = {out.x, out.y, out.z};
  if (!data::read_numbers(n, key, v, 3, e, required)) return false;
  out = {v[0], v[1], v[2]};
  return true;
}

void read_terrain(const data::Node& n, Terrain& t, data::Errors& e) {
  data::check_fields(n, {"size", "resolution", "noise_height", "noise_scale", "seed", "color"}, e);
  data::read_number(n, "size", t.size, e);
  float res = static_cast<float>(t.resolution), seed = static_cast<float>(t.seed);
  data::read_number(n, "resolution", res, e);
  data::read_number(n, "seed", seed, e);
  t.resolution = std::clamp(static_cast<int>(res), 2, 1025);
  t.seed = static_cast<int>(seed);
  data::read_number(n, "noise_height", t.noise_height, e);
  data::read_number(n, "noise_scale", t.noise_scale, e);
  read_vec3(n, "color", t.color, e, false);

  for (const data::Node& c : n.children) {
    if (c.type == "hill") {
      Hill h;
      h.name = c.name;
      data::check_fields(c, {"center", "radius", "height"}, e);
      read_vec2(c, "center", h.center, e, true);
      data::read_number(c, "radius", h.radius, e);
      data::read_number(c, "height", h.height, e);
      t.hills.push_back(h);
    } else if (c.type == "ridge") {
      Ridge r;
      r.name = c.name;
      data::check_fields(c, {"from", "to", "width", "height"}, e);
      read_vec2(c, "from", r.from, e, true);
      read_vec2(c, "to", r.to, e, true);
      data::read_number(c, "width", r.width, e);
      data::read_number(c, "height", r.height, e);
      t.ridges.push_back(r);
    } else if (c.type == "clearing") {
      Clearing cl;
      cl.name = c.name;
      data::check_fields(c, {"center", "radius", "blend", "height"}, e);
      read_vec2(c, "center", cl.center, e, true);
      data::read_number(c, "radius", cl.radius, e);
      data::read_number(c, "blend", cl.blend, e);
      data::read_number(c, "height", cl.height, e);
      t.clearings.push_back(cl);
    } else {
      e.add(c.file, c.line, "terrain can contain 'hill', 'ridge' or 'clearing', not '" + c.type + "'" +
                                data::did_you_mean(c.type, {"hill", "ridge", "clearing"}));
    }
  }
}

}  // namespace

float Terrain::height_at(float x, float z) const {
  Vec2 p{x, z};
  float h = 0.0f;
  if (noise_height > 0.0f && noise_scale > 0.0f) {
    float s = 1.0f / noise_scale;
    h += noise_height * (value_noise(x * s, z * s, seed) +
                         0.5f * value_noise(x * s * 2.1f, z * s * 2.1f, seed + 1) +
                         0.25f * value_noise(x * s * 4.3f, z * s * 4.3f, seed + 2));
  }
  for (const Hill& hl : hills) h += hl.height * bump(length(p - hl.center) / hl.radius);
  for (const Ridge& r : ridges) h += r.height * bump(distance_to_segment(p, r.from, r.to) / r.width);
  for (const Clearing& c : clearings) {
    float d = length(p - c.center);
    float w = 1.0f - smoothstepf(c.radius, c.radius + c.blend, d);
    h = lerpf(h, c.height, w);
  }
  return h;
}

Vec3 Terrain::normal_at(float x, float z) const {
  float e = std::max(size / static_cast<float>(resolution - 1), 0.05f);
  float dx = height_at(x + e, z) - height_at(x - e, z);
  float dz = height_at(x, z + e) - height_at(x, z - e);
  return normalize(Vec3{-dx, 2.0f * e, -dz});
}

bool build_region(const data::Node& root, Region& out, data::Errors& errors) {
  size_t before = errors.messages.size();
  out = Region{};
  for (const data::Node& n : root.children) {
    if (n.type == "terrain") {
      read_terrain(n, out.terrain, errors);
    } else if (n.type == "prop") {
      PropPlacement p;
      p.name = n.name;
      data::check_fields(n, {"position", "size", "yaw", "color", "shape"}, errors);
      read_vec2(n, "position", p.position, errors, true);
      std::string shape = "box";
      data::read_text(n, "shape", shape, errors);
      if (shape == "figure") {
        p.shape = PropShape::Figure;
        p.size = {0.7f, 1.75f, 0.7f};
      } else if (shape != "box") {
        const data::Field* f = n.find("shape");
        errors.add(n.file, f ? f->line : n.line,
                   "'shape' can be 'box' or 'figure', not '" + shape + "'" +
                       data::did_you_mean(shape, {"box", "figure"}));
      }
      read_vec3(n, "size", p.size, errors, false);
      data::read_number(n, "yaw", p.yaw, errors);
      read_vec3(n, "color", p.color, errors, false);
      out.props.push_back(p);
    } else if (n.type == "player") {
      data::check_fields(n, {"start", "facing"}, errors);
      read_vec2(n, "start", out.player.position, errors, false);
      data::read_number(n, "facing", out.player.facing, errors);
    } else if (n.type == "camera_zone") {
      CameraZone z;
      z.name = n.name;
      data::check_fields(n, {"framing", "center", "radius", "focus"}, errors);
      std::string framing;
      data::read_text(n, "framing", framing, errors, true);
      if (framing == "vista") z.framing = Framing::Vista;
      else if (framing == "duel") z.framing = Framing::Duel;
      else if (framing == "conversation") z.framing = Framing::Conversation;
      else if (framing == "explore") z.framing = Framing::Explore;
      else if (!framing.empty()) {
        const data::Field* f = n.find("framing");
        errors.add(n.file, f ? f->line : n.line,
                   "'framing' can be 'explore', 'vista', 'duel' or 'conversation', not '" + framing + "'" +
                       data::did_you_mean(framing, {"explore", "vista", "duel", "conversation"}));
      }
      read_vec2(n, "center", z.center, errors, true);
      data::read_number(n, "radius", z.radius, errors);
      z.has_focus = n.find("focus") != nullptr;
      read_vec2(n, "focus", z.focus, errors, false);
      if ((z.framing == Framing::Duel || z.framing == Framing::Conversation) && !z.has_focus)
        errors.add(n.file, n.line, "a '" + framing + "' zone needs a 'focus' (where the other person stands)");
      out.camera_zones.push_back(z);
    } else if (n.type == "camera") {
      data::check_fields(n, {"target", "distance", "yaw", "pitch"}, errors);
      read_vec2(n, "target", out.camera.target, errors, false);
      data::read_number(n, "distance", out.camera.distance, errors);
      data::read_number(n, "yaw", out.camera.yaw, errors);
      data::read_number(n, "pitch", out.camera.pitch, errors);
    } else {
      errors.add(n.file, n.line,
                 "a region can contain 'terrain', 'prop', 'player', 'camera' or 'camera_zone', not '" +
                     n.type + "'" +
                     data::did_you_mean(n.type, {"terrain", "prop", "player", "camera", "camera_zone"}));
    }
  }
  return errors.messages.size() == before;
}

bool load_region(const std::string& path, Region& out, data::Errors& errors) {
  data::Node root;
  if (!data::load_file(path, root, errors)) return false;
  return build_region(root, out, errors);
}

Mesh build_terrain_mesh(const Terrain& t) {
  Mesh m;
  const int n = t.resolution;
  const float step = t.size / static_cast<float>(n - 1);
  const float origin = -t.size * 0.5f;
  m.vertices.reserve(static_cast<size_t>(n) * static_cast<size_t>(n));
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      float x = origin + static_cast<float>(i) * step;
      float z = origin + static_cast<float>(j) * step;
      m.vertices.push_back({{x, t.height_at(x, z), z}, t.normal_at(x, z)});
    }
  }
  m.indices.reserve(static_cast<size_t>(n - 1) * static_cast<size_t>(n - 1) * 6);
  for (int j = 0; j + 1 < n; ++j) {
    for (int i = 0; i + 1 < n; ++i) {
      u32 d = static_cast<u32>(j * n + i);          // (x0, z0)
      u32 c = d + 1;                                // (x1, z0)
      u32 a = static_cast<u32>((j + 1) * n + i);    // (x0, z1)
      u32 b = a + 1;                                // (x1, z1)
      for (u32 idx : {a, b, c, a, c, d}) m.indices.push_back(idx);
    }
  }
  return m;
}

Mat4 prop_transform(const Terrain& terrain, const PropPlacement& p) {
  float yaw = radians(p.yaw);
  float c = std::cos(yaw), s = std::sin(yaw);
  float hx = p.size.x * 0.5f, hz = p.size.z * 0.5f;
  float lowest = terrain.height_at(p.position.x, p.position.y);
  for (float sx : {-1.0f, 1.0f}) {
    for (float sz : {-1.0f, 1.0f}) {
      // Footprint corner rotated by yaw (same rotation as rotate_y).
      float lx = sx * hx, lz = sz * hz;
      float wx = p.position.x + c * lx + s * lz;
      float wz = p.position.y - s * lx + c * lz;
      lowest = std::min(lowest, terrain.height_at(wx, wz));
    }
  }
  Vec3 center{p.position.x, lowest + p.size.y * 0.5f, p.position.y};
  return translate(center) * rotate_y(yaw) * scale(p.size);
}

}  // namespace rk
