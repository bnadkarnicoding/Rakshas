// tests/test_camera.cpp — the hybrid camera director.
#include "camera_director.h"
#include "test.h"

using namespace rk;

namespace {

Terrain flat() {
  Terrain t;
  t.size = 300.0f;
  t.noise_height = 0.0f;
  return t;
}

Character standing_at(Vec2 p) {
  Character c;
  c.position = {p.x, 0.0f, p.y};
  return c;
}

void run(CameraDirector& d, const Character& c, const Terrain& t,
         const std::vector<CameraZone>& zones, float seconds, CameraInput in = {}) {
  for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) d.update(c, t, zones, in, 1.0f / 60.0f);
}

float wrapped_diff(float a, float b) {
  float d = a - b;
  while (d > kPi) d -= 2 * kPi;
  while (d < -kPi) d += 2 * kPi;
  return std::fabs(d);
}

}  // namespace

TEST(zone_weight_is_smooth_and_bounded) {
  CameraZone z{"z", Framing::Vista, {0, 0}, 10.0f, false, {}};
  CHECK_NEAR(zone_weight(z, {0, 0}), 1.0f, 1e-6f);
  CHECK_NEAR(zone_weight(z, {12, 0}), 0.0f, 1e-6f);
  float mid = zone_weight(z, {8, 0});
  CHECK(mid > 0.0f && mid < 1.0f);
}

TEST(lerp_angle_takes_the_short_way) {
  float a = radians(170.0f), b = radians(-170.0f);
  float m = lerp_angle(a, b, 0.5f);
  CHECK(wrapped_diff(m, kPi) < 1e-4f);
}

TEST(explore_follows_player_input) {
  Terrain t = flat();
  CameraDirector d;
  Character c = standing_at({0, 0});
  d.reset(c, 0.0f);
  run(d, c, t, {}, 1.0f, CameraInput{0.01f, 0.0f, 0.0f});  // turn steadily
  CHECK_NEAR(d.rig().yaw, 0.6f, 0.02f);                       // 60 frames * 0.01
  CHECK(d.framing() == Framing::Explore);
}

TEST(vista_pulls_back_and_looks_at_the_view) {
  Terrain t = flat();
  std::vector<CameraZone> zones = {{"ridge", Framing::Vista, {50, 0}, 15.0f, true, {0, 0}}};
  CameraDirector d;
  Character c = standing_at({50, 0});
  d.reset(c, radians(90.0f));
  run(d, c, t, zones, 4.0f);
  CHECK(d.framing() == Framing::Vista);
  CHECK_NEAR(d.rig().distance, framing_params(Framing::Vista).distance, 0.1f);
  // Looking west toward (0,0): the camera sits east of the player.
  CHECK(d.rig().eye().x > 50.0f);
}

TEST(duel_keeps_the_opponent_in_front) {
  Terrain t = flat();
  std::vector<CameraZone> zones = {{"duel", Framing::Duel, {0, 0}, 8.0f, true, {0, -5}}};
  CameraDirector d;
  Character c = standing_at({0, 0});
  d.reset(c, radians(180.0f));  // start looking the wrong way
  run(d, c, t, zones, 4.0f);
  Vec3 eye = d.rig().eye();
  Vec3 to_opponent = normalize(Vec3{0, 1.5f, -5} - eye);
  Vec3 view = normalize(d.rig().look_at_point() - eye);
  CHECK(dot(to_opponent, view) > 0.95f);
  CHECK(d.rig().side > 0.6f);  // over the shoulder
}

TEST(conversation_frames_both_people_side_on) {
  Terrain t = flat();
  std::vector<CameraZone> zones = {{"talk", Framing::Conversation, {0, 0}, 6.0f, true, {3, 0}}};
  CameraDirector d;
  Character c = standing_at({0, 0});
  d.reset(c, 0.0f);
  run(d, c, t, zones, 5.0f);
  Vec3 eye = d.rig().eye();
  Vec3 view = normalize(d.rig().look_at_point() - eye);
  CHECK(std::fabs(view.x) < 0.15f);         // looking across the line between them
  CHECK_NEAR(d.rig().look_at_point().x, 1.5f, 0.1f);  // centred between the two
}

TEST(nudge_drifts_back_but_directed_mode_ignores_it) {
  Terrain t = flat();
  std::vector<CameraZone> zones = {{"duel", Framing::Duel, {0, 0}, 8.0f, true, {0, -5}}};
  Character c = standing_at({0, 0});

  CameraDirector d;
  d.reset(c, 0.0f);
  run(d, c, t, zones, 3.0f);
  float settled = d.rig().yaw;
  run(d, c, t, zones, 0.5f, CameraInput{0.02f, 0, 0});  // player nudges
  CHECK(wrapped_diff(d.rig().yaw, settled) > radians(10.0f));
  run(d, c, t, zones, 5.0f);  // lets go
  CHECK(wrapped_diff(d.rig().yaw, settled) < radians(2.0f));

  CameraDirector strict;
  strict.directed_only = true;
  strict.reset(c, 0.0f);
  run(strict, c, t, zones, 3.0f);
  float before = strict.rig().yaw;
  run(strict, c, t, zones, 0.5f, CameraInput{0.02f, 0, 0});
  CHECK(wrapped_diff(strict.rig().yaw, before) < radians(0.5f));
}

TEST(walking_through_zones_never_cuts) {
  // Principle 7: one continuous camera. Walk at running speed through every
  // kind of zone and check the eye never jumps between frames.
  Terrain t = flat();
  std::vector<CameraZone> zones = {
      {"vista", Framing::Vista, {0, -20}, 10.0f, true, {0, 40}},
      {"duel", Framing::Duel, {0, -45}, 7.0f, true, {3, -48}},
      {"talk", Framing::Conversation, {0, -70}, 5.0f, true, {2, -71}}};
  CameraDirector d;
  Character c = standing_at({0, 0});
  d.reset(c, 0.0f);
  Vec3 last = d.rig().eye();
  float worst = 0.0f;
  for (int i = 0; i < 60 * 20; ++i) {  // 20 s at 5 m/s = 100 m north
    c.position.z -= 5.0f / 60.0f;
    d.update(c, t, zones, {}, 1.0f / 60.0f);
    Vec3 eye = d.rig().eye();
    worst = std::fmax(worst, length(eye - last));
    last = eye;
  }
  CHECK(worst < 0.35f);  // metres per frame at 60 fps
}
