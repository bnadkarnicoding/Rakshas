// game/character.h — the player character's movement: walking, running,
// following the ground and being stopped by solid objects.
//
// This is pure simulation (no rendering, no input devices), stepped at a fixed
// rate so it behaves the same on fast and slow machines (ARCHITECTURE.md §4).
#pragma once
#include <vector>

#include "core/math.h"
#include "region.h"

namespace rk {

struct CharacterParams {
  float radius = 0.35f;          // metres (collision circle seen from above)
  float height = 1.75f;          // metres
  float walk_speed = 2.0f;       // m/s
  float run_speed = 5.0f;        // m/s
  float acceleration = 10.0f;    // how quickly speed changes (higher = snappier)
  float turn_rate = 12.0f;       // how quickly the body turns to face its direction
  float max_slope_degrees = 45;  // steeper ground can't be walked up
};

struct Character {
  Vec3 position;     // feet, on the ground
  Vec2 velocity;     // horizontal, m/s (x, z)
  float facing = 0;  // radians; 0 faces north (-Z), matching rotate_y
};

// A solid object seen from above: an oriented rectangle.
struct Obstacle {
  Vec2 center;
  Vec2 half_size;     // half width (x) and half depth (z) before rotation
  float yaw = 0.0f;   // radians, same rotation as rotate_y
};

Obstacle obstacle_from_prop(const PropPlacement& prop);

// What the player is asking for this step.
struct MoveIntent {
  Vec2 direction;     // world-space x z, length 0..1 (stick tilt)
  bool run = false;
};

// Advances the character by `dt` seconds.
void step_character(Character& c, const CharacterParams& p, const Terrain& terrain,
                    const std::vector<Obstacle>& obstacles, const MoveIntent& intent, float dt);

// Pushes a circle at `pos` out of `box` if they overlap. Returns the new position.
Vec2 push_out_of_obstacle(Vec2 pos, float radius, const Obstacle& box);

// Facing angle (radians) for a horizontal direction, in rotate_y's convention.
float facing_for_direction(Vec2 dir);

}  // namespace rk
