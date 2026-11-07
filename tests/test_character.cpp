// tests/test_character.cpp — walking, running, slopes and collisions.
#include "character.h"
#include "test.h"

using namespace rk;

static Vec2 normalize3(Vec2 v) {
  float l = length(v);
  return l > 0 ? v * (1.0f / l) : v;
}

static Terrain flat_terrain() {
  Terrain t;
  t.size = 200.0f;
  t.noise_height = 0.0f;
  return t;
}

static void simulate(Character& c, const Terrain& t, const std::vector<Obstacle>& obs,
                     MoveIntent intent, float seconds, const CharacterParams& p = {}) {
  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) step_character(c, p, t, obs, intent, dt);
}

TEST(character_walks_and_runs_at_set_speeds) {
  Terrain t = flat_terrain();
  CharacterParams p;
  Character walker, runner;
  simulate(walker, t, {}, {{0, -1}, false}, 2.0f, p);
  simulate(runner, t, {}, {{0, -1}, true}, 2.0f, p);
  CHECK_NEAR(length(walker.velocity), p.walk_speed, 0.05f);
  CHECK_NEAR(length(runner.velocity), p.run_speed, 0.05f);
  CHECK(runner.position.z < walker.position.z);  // further north
}

TEST(character_eases_to_a_stop) {
  Terrain t = flat_terrain();
  Character c;
  simulate(c, t, {}, {{1, 0}, true}, 1.0f);
  simulate(c, t, {}, {{0, 0}, false}, 1.0f);
  CHECK(length(c.velocity) < 0.01f);
}

TEST(character_turns_to_face_where_it_walks) {
  Terrain t = flat_terrain();
  Character c;
  simulate(c, t, {}, {{-1, 0}, false}, 1.5f);  // walk west
  CHECK_NEAR(c.facing, radians(90.0f), 0.05f);
}

TEST(character_follows_the_ground) {
  Terrain t = flat_terrain();
  t.hills.push_back({"h", {0, -10}, 30.0f, 4.0f});
  Character c;
  c.position = {0, t.height_at(0, 10), 10};
  simulate(c, t, {}, {{0, -1}, false}, 4.0f);
  CHECK_NEAR(c.position.y, t.height_at(c.position.x, c.position.z), 1e-4f);
  CHECK(c.position.y > 1.0f);  // climbed partway up the hill
}

TEST(push_out_of_axis_aligned_and_rotated_boxes) {
  Obstacle box{{0, 0}, {1, 1}, 0.0f};
  Vec2 p = push_out_of_obstacle({1.2f, 0.0f}, 0.5f, box);
  CHECK_NEAR(p.x, 1.5f, 1e-4f);
  CHECK_NEAR(p.y, 0.0f, 1e-4f);
  Vec2 untouched = push_out_of_obstacle({3.0f, 0.0f}, 0.5f, box);
  CHECK_NEAR(untouched.x, 3.0f, 1e-6f);

  Obstacle rotated{{5, 5}, {2, 0.5f}, radians(45.0f)};
  for (float a = 0; a < 6.28f; a += 0.3f) {
    Vec2 q = push_out_of_obstacle({5 + std::cos(a) * 0.3f, 5 + std::sin(a) * 0.3f}, 0.4f, rotated);
    // After pushing, the point must be outside the box grown by the radius.
    Vec2 again = push_out_of_obstacle(q, 0.4f - 1e-3f, rotated);
    CHECK_NEAR(again.x, q.x, 1e-3f);
    CHECK_NEAR(again.y, q.y, 1e-3f);
  }
}

TEST(character_never_overlaps_a_hut) {
  Terrain t = flat_terrain();
  CharacterParams p;
  std::vector<Obstacle> obs = {{{0, -5}, {1.5f, 1.5f}, radians(30.0f)}};
  const float dt = 1.0f / 60.0f;
  for (Vec2 dir : {Vec2{0, -1}, Vec2{0.3f, -1}, Vec2{-0.2f, -1}}) {
    Character c;
    bool ever_inside = false;
    for (int i = 0; i < 300; ++i) {
      step_character(c, p, t, obs, {normalize3(dir), true}, dt);
      Vec2 pos{c.position.x, c.position.z};
      Vec2 out = push_out_of_obstacle(pos, p.radius - 0.01f, obs[0]);
      if (length(out - pos) > 1e-3f) ever_inside = true;
    }
    CHECK(!ever_inside);
  }
}

TEST(character_stops_at_a_square_wall) {
  Terrain t = flat_terrain();
  CharacterParams p;
  std::vector<Obstacle> obs = {{{0, -5}, {3.0f, 0.5f}, 0.0f}};  // a wall facing the player
  Character c;
  simulate(c, t, obs, {{0, -1}, true}, 4.0f, p);
  CHECK_NEAR(c.position.z, -4.5f + p.radius, 1e-3f);  // pressed against its south face
}

TEST(character_cannot_climb_a_cliff) {
  Terrain t = flat_terrain();
  t.ridges.push_back({"cliff", {-50, -10}, {50, -10}, 3.0f, 20.0f});  // very steep
  Character c;
  simulate(c, t, {}, {{0, -1}, true}, 6.0f);
  CHECK(c.position.y < 8.0f);  // stopped low on the slope, nowhere near the top
}

TEST(character_stays_inside_the_land) {
  Terrain t = flat_terrain();
  Character c;
  simulate(c, t, {}, {{1, 0}, true}, 40.0f);
  CHECK(c.position.x <= t.size * 0.5f);
}
