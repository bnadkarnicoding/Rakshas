// game/camera_director.h — the hybrid camera (CorePrinciples Tier 3 #1, #5, #7).
//
// One continuous camera that changes its framing by context and never cuts or
// shakes. Places in the world are marked with camera zones; as the player
// walks into one, the framing glides toward that zone's framing:
//
//   Explore       medium distance behind the player (default, player-steered)
//   Vista         pulls back, low and wide, looking out over the land
//   Duel          tight over the shoulder, keeping the opponent in view
//   Conversation  a calm two-shot framing the player and the other person
//
// In a directed zone the player may still nudge the view; it drifts back when
// they let go. `directed_only` turns nudging off (CorePrinciples Q8 is open).
#pragma once
#include <string>
#include <vector>

#include "character.h"
#include "core/math.h"
#include "region.h"

namespace rk {

// How a framing places the camera relative to what it looks at.
struct FramingParams {
  float distance = 6.0f;   // metres from the look point
  float pitch = 14.0f;     // degrees above the horizon
  float fov = 55.0f;       // vertical field of view, degrees
  float side = 0.0f;       // metres to the right (over-the-shoulder)
  float height = 1.5f;     // look-point height above the player's feet
};

FramingParams framing_params(Framing f);

// What the camera actually is this frame (always changes smoothly).
struct CameraRig {
  Vec3 focus;              // the point the framing is built around
  float yaw = 0.0f;        // radians; camera sits at focus + (sin yaw, ., cos yaw) * distance
  float pitch = radians(14.0f);
  float distance = 6.0f;
  float fov = radians(55.0f);
  float side = 0.0f;

  Vec3 right() const { return {std::cos(yaw), 0.0f, -std::sin(yaw)}; }
  Vec3 look_at_point() const { return focus + right() * side; }
  Vec3 eye() const {
    float c = std::cos(pitch);
    return look_at_point() + Vec3{std::sin(yaw) * c, std::sin(pitch), std::cos(yaw) * c} * distance;
  }
};

struct CameraInput {
  float yaw = 0.0f;    // radians to turn this frame (+ turns the view left)
  float pitch = 0.0f;  // radians to tilt this frame (+ moves the camera up, looking down)
  float zoom = 0.0f;   // + further away (Explore only)
};

// 0 outside the zone, rising smoothly to 1 well inside it.
float zone_weight(const CameraZone& zone, Vec2 position);

// Shortest-way interpolation between two angles (radians).
float lerp_angle(float a, float b, float t);

class CameraDirector {
 public:
  bool directed_only = false;

  void reset(const Character& player, float yaw);
  void update(const Character& player, const Terrain& terrain,
              const std::vector<CameraZone>& zones, const CameraInput& input, float dt);

  const CameraRig& rig() const { return rig_; }
  Framing framing() const { return framing_; }
  const std::string& zone_name() const { return zone_name_; }
  float zone_strength() const { return weight_; }
  // Framing of the strongest nearby zone, even before it fully takes over.
  Framing zone_framing() const { return zone_framing_; }

 private:
  CameraRig rig_;
  Framing framing_ = Framing::Explore;
  std::string zone_name_;
  float weight_ = 0.0f;
  Framing zone_framing_ = Framing::Explore;
  float explore_distance_ = 6.0f;
  float pitch_offset_ = 0.0f;  // player's tilt in Explore
  float nudge_yaw_ = 0.0f;     // player's nudge inside a directed zone
  float nudge_pitch_ = 0.0f;
  float idle_time_ = 0.0f;     // seconds since the last look input
};

}  // namespace rk
