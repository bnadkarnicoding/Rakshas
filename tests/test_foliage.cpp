// tests/test_foliage.cpp — tree and grass placement rules.
#include "foliage.h"
#include "test.h"

using namespace rk;

static Region test_region() {
  Region r;
  r.terrain.size = 300.0f;
  r.terrain.noise_height = 0.0f;
  r.terrain.clearings.push_back({"village", {0, 0}, 12.0f, 6.0f, 0.0f});
  r.terrain.ridges.push_back({"cliff", {40, -60}, {40, 60}, 3.0f, 15.0f});  // very steep
  PropPlacement hut;
  hut.position = {-20, 0};
  hut.size = {3.6f, 3.0f, 3.6f};
  r.props.push_back(hut);
  r.forests.push_back({"woods", {0, 0}, 60.0f, 200, 7, 3.5f});
  r.grasses.push_back({"meadow", {0, 0}, 30.0f, 1.0f, 3});
  return r;
}

TEST(trees_avoid_clearings_props_and_cliffs) {
  Region r = test_region();
  std::vector<TreePlacement> trees = place_trees(r);
  CHECK(trees.size() > 50);
  for (const TreePlacement& t : trees) {
    CHECK(length(t.position) > 12.0f + 3.0f - 1e-3f);           // not in the village
    CHECK(length(t.position - Vec2{-20, 0}) > 2.5f);             // not on the hut
    CHECK(r.terrain.normal_at(t.position.x, t.position.y).y >= std::cos(radians(28.0f)) - 1e-4f);
  }
}

TEST(trees_keep_their_distance) {
  Region r = test_region();
  std::vector<TreePlacement> trees = place_trees(r);
  for (size_t i = 0; i < trees.size(); ++i)
    for (size_t j = i + 1; j < trees.size(); ++j)
      CHECK(length(trees[i].position - trees[j].position) >= 3.5f * 0.5f * (0.75f + 1.0f) - 1e-3f);
}

TEST(placement_is_repeatable) {
  Region r = test_region();
  std::vector<TreePlacement> a = place_trees(r), b = place_trees(r);
  CHECK(a.size() == b.size());
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    CHECK(a[i].position.x == b[i].position.x);
    CHECK(a[i].position.y == b[i].position.y);
  }
}

TEST(grass_is_thinner_in_the_village_and_scales_down) {
  Region r = test_region();
  std::vector<FoliageInstance> full = place_grass(r, 1.0f);
  std::vector<FoliageInstance> low = place_grass(r, 0.5f);
  CHECK(full.size() > 1000);
  CHECK(low.size() * 10 < full.size() * 6);
  int inner = 0, outer = 0;
  for (const FoliageInstance& g : full) {
    float d = length(Vec2{g.position_yaw.x, g.position_yaw.z});
    if (d < 6.0f) ++inner;
    else if (d > 20.0f && d < 26.0f) ++outer;
  }
  // Same-sized rings: the outer one (wild grass) has far more than the centre.
  float inner_area = kPi * 36.0f, outer_area = kPi * (26.0f * 26.0f - 20.0f * 20.0f);
  CHECK(static_cast<float>(inner) / inner_area < 0.3f * (static_cast<float>(outer) / outer_area));
}
