// game/character.cpp — character movement and collision.
#include "character.h"

#include <algorithm>

namespace rk {
namespace {

// Rotates a horizontal vector by `yaw` the same way rotate_y does (x z plane).
Vec2 rotate(Vec2 v, float yaw) {
  float c = std::cos(yaw), s = std::sin(yaw);
  return {c * v.x + s * v.y, -s * v.x + c * v.y};
}

float wrap_angle(float a) {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

}  // namespace

Obstacle obstacle_from_prop(const PropPlacement& prop) {
  if (prop.shape == PropShape::Figure || prop.shape == PropShape::Model) {
    // A person or a round model (e.g. a hut): a square slightly inside the round outline.
    float r = prop.size.x * 0.5f * 0.85f;
    return {prop.position, {r, r}, 0.0f};
  }
  return {prop.position, {prop.size.x * 0.5f, prop.size.z * 0.5f}, radians(prop.yaw)};
}

float facing_for_direction(Vec2 dir) { return std::atan2(-dir.x, -dir.y); }

Vec2 push_out_of_obstacle(Vec2 pos, float radius, const Obstacle& box) {
  // Work in the box's own frame, where it is an axis-aligned rectangle.
  Vec2 local = rotate(pos - box.center, -box.yaw);
  Vec2 closest{clampf(local.x, -box.half_size.x, box.half_size.x),
               clampf(local.y, -box.half_size.y, box.half_size.y)};
  Vec2 diff = local - closest;
  float dist = length(diff);
  if (dist >= radius) return pos;

  Vec2 pushed;
  if (dist > 1e-5f) {
    pushed = closest + diff * (radius / dist);
  } else {
    // Centre is inside the rectangle: leave through the nearest side.
    float dx = box.half_size.x - std::fabs(local.x);
    float dz = box.half_size.y - std::fabs(local.y);
    pushed = local;
    if (dx < dz) pushed.x = (local.x < 0 ? -1.0f : 1.0f) * (box.half_size.x + radius);
    else pushed.y = (local.y < 0 ? -1.0f : 1.0f) * (box.half_size.y + radius);
  }
  return box.center + rotate(pushed, box.yaw);
}

void step_character(Character& c, const CharacterParams& p, const Terrain& terrain,
                    const std::vector<Obstacle>& obstacles, const MoveIntent& intent, float dt) {
  // 1. Ease the velocity toward what the player asks for.
  float speed = intent.run ? p.run_speed : p.walk_speed;
  Vec2 target = intent.direction * speed;
  float blend = 1.0f - std::exp(-p.acceleration * dt);
  c.velocity = c.velocity + (target - c.velocity) * blend;

  // 2. Proposed new position.
  Vec2 pos{c.position.x, c.position.z};
  Vec2 next = pos + c.velocity * dt;

  // 3. Too steep uphill? Then that step doesn't happen.
  float min_up = std::cos(radians(p.max_slope_degrees));
  if (terrain.normal_at(next.x, next.y).y < min_up &&
      terrain.height_at(next.x, next.y) > terrain.height_at(pos.x, pos.y)) {
    next = pos;
    c.velocity = {0.0f, 0.0f};
  }

  // 4. Solid objects push the character back out (twice, for corners).
  for (int pass = 0; pass < 2; ++pass)
    for (const Obstacle& o : obstacles) next = push_out_of_obstacle(next, p.radius, o);

  // 5. Stay inside the land.
  float limit = terrain.size * 0.5f - 2.0f;
  next.x = clampf(next.x, -limit, limit);
  next.y = clampf(next.y, -limit, limit);

  // Velocity reflects where we actually went (so sliding along walls feels right).
  if (dt > 0.0f) c.velocity = (next - pos) * (1.0f / dt);

  // 6. Feet on the ground.
  c.position = {next.x, terrain.height_at(next.x, next.y), next.y};

  // 7. Turn smoothly to face the direction of travel.
  if (length(c.velocity) > 0.2f) {
    float want = facing_for_direction(c.velocity);
    float delta = wrap_angle(want - c.facing);
    c.facing = wrap_angle(c.facing + delta * (1.0f - std::exp(-p.turn_rate * dt)));
  }
}

}  // namespace rk
