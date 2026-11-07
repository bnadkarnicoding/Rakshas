// game/foliage.cpp — tree and grass placement.
#include "foliage.h"

namespace rk {
namespace {

// Small deterministic random generator (xorshift), so placement never depends
// on the platform's library.
struct Random {
  u32 state;
  explicit Random(int seed) : state(static_cast<u32>(seed) * 2654435761u + 0x9E3779B9u) {
    if (state == 0) state = 1;
  }
  u32 next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  float unit() { return static_cast<float>(next() & 0xFFFFFFu) / static_cast<float>(0xFFFFFF); }
  float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

Vec2 point_in_circle(Random& rng, Vec2 center, float radius) {
  float r = radius * std::sqrt(rng.unit());
  float a = rng.range(0.0f, 2.0f * kPi);
  return center + Vec2{std::cos(a) * r, std::sin(a) * r};
}

bool in_clearing(const Terrain& t, Vec2 p, float margin) {
  for (const Clearing& c : t.clearings)
    if (length(p - c.center) < c.radius + margin) return true;
  return false;
}

bool near_prop(const Region& r, Vec2 p, float margin) {
  for (const PropPlacement& prop : r.props) {
    float reach = 0.5f * std::max(prop.size.x, prop.size.z) * 1.42f + margin;
    if (length(p - prop.position) < reach) return true;
  }
  return false;
}

bool too_steep(const Terrain& t, Vec2 p, float max_degrees) {
  return t.normal_at(p.x, p.y).y < std::cos(radians(max_degrees));
}

bool inside_land(const Terrain& t, Vec2 p) {
  float limit = t.size * 0.5f - 4.0f;
  return std::fabs(p.x) < limit && std::fabs(p.y) < limit;
}

}  // namespace

std::vector<TreePlacement> place_trees(const Region& region) {
  std::vector<TreePlacement> trees;
  const Terrain& t = region.terrain;
  for (const ForestArea& f : region.forests) {
    Random rng(f.seed);
    int placed = 0;
    for (int attempt = 0; attempt < f.count * 30 && placed < f.count; ++attempt) {
      Vec2 p = point_in_circle(rng, f.center, f.radius);
      if (!inside_land(t, p) || in_clearing(t, p, 3.0f) || near_prop(region, p, 2.5f) ||
          too_steep(t, p, 28.0f))
        continue;
      bool crowded = false;
      for (const TreePlacement& other : trees)
        if (length(other.position - p) < f.spacing * 0.5f * (other.scale + 1.0f)) crowded = true;
      if (crowded) continue;
      TreePlacement tree;
      tree.position = p;
      tree.yaw = rng.range(0.0f, 2.0f * kPi);
      tree.scale = rng.range(0.75f, 1.3f);
      tree.phase = rng.range(0.0f, 6.28f);
      float g = rng.range(0.85f, 1.15f);
      tree.tint = {g * rng.range(0.9f, 1.1f), g, g * rng.range(0.85f, 1.05f)};
      trees.push_back(tree);
      ++placed;
    }
  }
  return trees;
}

std::vector<FoliageInstance> tree_instances(const Region& region, const std::vector<TreePlacement>& trees) {
  std::vector<FoliageInstance> out;
  out.reserve(trees.size());
  for (const TreePlacement& tree : trees) {
    float h = region.terrain.height_at(tree.position.x, tree.position.y) - 0.1f;  // trunk sits in the soil
    out.push_back({{tree.position.x, h, tree.position.y, tree.yaw},
                   {tree.scale, tree.scale, tree.phase, 0.0f},
                   {tree.tint.x, tree.tint.y, tree.tint.z, 1.0f}});
  }
  return out;
}

std::vector<Obstacle> tree_obstacles(const std::vector<TreePlacement>& trees) {
  std::vector<Obstacle> out;
  for (const TreePlacement& tree : trees) {
    float half = 0.24f * tree.scale;
    out.push_back({tree.position, {half, half}, 0.0f});
  }
  return out;
}

std::vector<FoliageInstance> place_grass(const Region& region, float density_scale) {
  std::vector<FoliageInstance> out;
  const Terrain& t = region.terrain;
  for (const GrassArea& g : region.grasses) {
    Random rng(g.seed);
    float area = kPi * g.radius * g.radius;
    int count = static_cast<int>(area * g.density * density_scale);
    for (int i = 0; i < count; ++i) {
      Vec2 p = point_in_circle(rng, g.center, g.radius);
      float yaw = rng.range(0.0f, 2.0f * kPi);
      float size = rng.range(0.7f, 1.35f);
      float phase = rng.range(0.0f, 6.28f);
      float dry = rng.unit();
      // Trampled ground: thinner grass toward clearing centres and around objects.
      if (!inside_land(t, p) || near_prop(region, p, 0.3f) || too_steep(t, p, 38.0f)) continue;
      bool skip = false;
      for (const Clearing& c : t.clearings) {
        float d = length(p - c.center) / std::max(c.radius, 0.01f);
        if (d < 1.0f && rng.unit() > d * d) skip = true;
      }
      if (skip) continue;
      Vec3 tint = lerp(Vec3{0.85f, 1.05f, 0.75f}, Vec3{1.25f, 1.05f, 0.65f}, dry);  // green .. straw
      out.push_back({{p.x, t.height_at(p.x, p.y) - 0.02f, p.y, yaw},
                     {size, size * rng.range(0.8f, 1.2f), phase, 0.0f},
                     {tint.x, tint.y, tint.z, 1.0f}});
    }
  }
  return out;
}

}  // namespace rk
