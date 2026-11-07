// game/camera_director.cpp — the hybrid camera's framing logic.
#include "camera_director.h"

#include <algorithm>

namespace rk {
namespace {

constexpr float kGlideRate = 2.0f;        // framing changes settle in ~1.5 s
constexpr float kFollowRate = 10.0f;      // how tightly the camera tracks the player
constexpr float kNudgeReturnDelay = 0.8f; // seconds after letting go before drifting back
constexpr float kNudgeReturnRate = 1.5f;
constexpr float kMaxDirectedTurn = 60.0f * kPi / 180.0f;  // rad/s: no whip-pans

float wrap(float a) {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

float approach(float current, float target, float rate, float dt) {
  return lerpf(current, target, 1.0f - std::exp(-rate * dt));
}

// Yaw that puts the camera behind `from`, looking toward `to`.
float yaw_looking(Vec2 from, Vec2 to) {
  Vec2 d = to - from;
  if (length(d) < 1e-4f) return 0.0f;
  return std::atan2(-d.x, -d.y);
}

FramingParams lerp_params(const FramingParams& a, const FramingParams& b, float t) {
  return {lerpf(a.distance, b.distance, t), lerpf(a.pitch, b.pitch, t), lerpf(a.fov, b.fov, t),
          lerpf(a.side, b.side, t), lerpf(a.height, b.height, t)};
}

}  // namespace

const char* framing_name(Framing f) {
  switch (f) {
    case Framing::Explore: return "explore";
    case Framing::Vista: return "vista";
    case Framing::Duel: return "duel";
    case Framing::Conversation: return "conversation";
  }
  return "explore";
}

FramingParams framing_params(Framing f) {
  switch (f) {
    case Framing::Explore: return {6.0f, 14.0f, 55.0f, 0.0f, 1.5f};
    case Framing::Vista: return {12.0f, 7.0f, 62.0f, 0.0f, 2.4f};
    case Framing::Duel: return {3.2f, 8.0f, 48.0f, 0.75f, 1.55f};
    case Framing::Conversation: return {5.0f, 5.0f, 42.0f, 0.0f, 1.45f};
  }
  return {};
}

float zone_weight(const CameraZone& zone, Vec2 position) {
  float d = length(position - zone.center);
  return 1.0f - smoothstepf(zone.radius * 0.55f, zone.radius, d);
}

float lerp_angle(float a, float b, float t) { return a + wrap(b - a) * t; }

void CameraDirector::reset(const Character& player, float yaw) {
  FramingParams p = framing_params(Framing::Explore);
  rig_ = CameraRig{};
  rig_.focus = player.position + Vec3{0, p.height, 0};
  rig_.yaw = yaw;
  rig_.pitch = radians(p.pitch);
  rig_.distance = p.distance;
  rig_.fov = radians(p.fov);
  explore_distance_ = p.distance;
  pitch_offset_ = nudge_yaw_ = nudge_pitch_ = 0.0f;
  framing_ = Framing::Explore;
  zone_name_.clear();
  weight_ = 0.0f;
}

void CameraDirector::update(const Character& player, const Terrain& terrain,
                            const std::vector<CameraZone>& zones, const CameraInput& input,
                            float dt) {
  Vec2 pos{player.position.x, player.position.z};

  // 1. Which zone (if any) is shaping the framing, and how strongly.
  const CameraZone* zone = nullptr;
  float w = 0.0f;
  for (const CameraZone& z : zones) {
    float zw = zone_weight(z, pos);
    if (zw > w) {
      w = zw;
      zone = &z;
    }
  }
  weight_ = w;
  framing_ = (zone && w > 0.5f) ? zone->framing : Framing::Explore;
  zone_name_ = (zone && w > 0.5f) ? zone->name : std::string();

  // 2. Player look input: steers freely in Explore; inside a zone it becomes a
  //    nudge that drifts back after they let go (unless fully directed).
  bool looking = std::fabs(input.yaw) + std::fabs(input.pitch) > 1e-6f;
  idle_time_ = looking ? 0.0f : idle_time_ + dt;
  float free_share = 1.0f - w;
  float base_yaw = rig_.yaw + input.yaw * free_share;
  pitch_offset_ = clampf(pitch_offset_ + input.pitch * free_share, radians(-35.0f), radians(55.0f));
  if (w > 0.0f && !directed_only) {
    nudge_yaw_ = clampf(nudge_yaw_ + input.yaw * w, radians(-70.0f), radians(70.0f));
    nudge_pitch_ = clampf(nudge_pitch_ + input.pitch * w, radians(-20.0f), radians(25.0f));
  }
  if (directed_only || w <= 0.0f || idle_time_ > kNudgeReturnDelay) {
    float keep = std::exp(-kNudgeReturnRate * dt);
    nudge_yaw_ *= keep;
    nudge_pitch_ *= keep;
  }
  explore_distance_ = clampf(explore_distance_ * std::exp(input.zoom), 2.5f, 30.0f);

  // 3. Where the zone wants the camera.
  FramingParams explore = framing_params(Framing::Explore);
  explore.distance = explore_distance_;
  FramingParams want = explore;
  float zone_yaw = base_yaw;
  Vec3 head = player.position + Vec3{0, explore.height, 0};
  Vec3 focus_point = head;
  if (zone) {
    FramingParams zp = framing_params(zone->framing);
    want = lerp_params(explore, zp, w);
    head = player.position + Vec3{0, want.height, 0};
    focus_point = head;
    if (zone->has_focus) {
      Vec3 other{zone->focus.x, terrain.height_at(zone->focus.x, zone->focus.y) + want.height,
                 zone->focus.y};
      switch (zone->framing) {
        case Framing::Vista:
          zone_yaw = yaw_looking(pos, zone->focus);
          break;
        case Framing::Duel:
          zone_yaw = yaw_looking(pos, zone->focus);
          focus_point = lerp(head, other, 0.3f * w);  // keep the opponent in frame
          break;
        case Framing::Conversation: {
          // Side-on two-shot: look across the line between the two speakers,
          // from whichever side the camera is already closer to.
          Vec2 d = zone->focus - pos;
          float a = std::atan2(-d.y, d.x);
          float b = wrap(a + kPi);
          zone_yaw = std::fabs(wrap(a - rig_.yaw)) < std::fabs(wrap(b - rig_.yaw)) ? a : b;
          focus_point = lerp(head, other, 0.5f * w);
          want.distance = std::max(want.distance, length(d) * 1.6f);
          break;
        }
        case Framing::Explore:
          break;
      }
    }
  }

  // 4. Glide every part of the framing toward its target. Nothing ever jumps.
  float target_yaw = lerp_angle(base_yaw, zone_yaw + nudge_yaw_, w);
  float turn = wrap(target_yaw - base_yaw) * (1.0f - std::exp(-kGlideRate * dt));
  turn = clampf(turn, -kMaxDirectedTurn * dt, kMaxDirectedTurn * dt);
  rig_.yaw = wrap(base_yaw + turn);
  float target_pitch = radians(want.pitch) + pitch_offset_ * free_share + nudge_pitch_ * w;
  rig_.pitch = approach(rig_.pitch, clampf(target_pitch, radians(-20.0f), radians(70.0f)), kGlideRate * 2.0f, dt);
  rig_.distance = approach(rig_.distance, want.distance, kGlideRate, dt);
  rig_.fov = approach(rig_.fov, radians(want.fov), kGlideRate, dt);
  rig_.side = approach(rig_.side, want.side, kGlideRate, dt);
  rig_.focus = lerp(rig_.focus, focus_point, 1.0f - std::exp(-kFollowRate * dt));

  // 5. Never let the camera sink into the ground.
  Vec3 look = rig_.look_at_point();
  Vec3 eye = rig_.eye();
  float min_pitch = std::asin(
      clampf((terrain.height_at(eye.x, eye.z) + 0.4f - look.y) / rig_.distance, -1.0f, 1.0f));
  if (rig_.pitch < min_pitch) rig_.pitch = min_pitch;
}

}  // namespace rk
