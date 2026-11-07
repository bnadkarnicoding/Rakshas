// game/foliage.h — placing trees and grass from a region's forest and grass
// areas. Placement is deterministic (same file + seed = same trees), avoids
// clearings, objects and steep ground, and keeps trees apart.
#pragma once
#include <vector>

#include "character.h"
#include "region.h"
#include "scene_constants.h"

namespace rk {

struct TreePlacement {
  Vec2 position;
  float yaw = 0.0f;
  float scale = 1.0f;   // overall size multiplier
  float phase = 0.0f;   // wind phase, so trees don't sway in lockstep
  Vec3 tint{1, 1, 1};
};

std::vector<TreePlacement> place_trees(const Region& region);

// `density_scale` lowers grass for the low graphics preset (e.g. 0.5).
std::vector<FoliageInstance> place_grass(const Region& region, float density_scale);

std::vector<FoliageInstance> tree_instances(const Region& region, const std::vector<TreePlacement>& trees);

// Trunks are solid: the player walks around them.
std::vector<Obstacle> tree_obstacles(const std::vector<TreePlacement>& trees);

}  // namespace rk
