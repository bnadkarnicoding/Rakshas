// tests/test_region.cpp — terrain shaping and region loading.
#include "region.h"
#include "test.h"

using namespace rk;

static Region parse_region(const char* text, data::Errors& errors) {
  data::Node root;
  Region r;
  data::parse(text, "test.rk", root, errors);
  build_region(root, r, errors);
  return r;
}

TEST(region_features_shape_the_land) {
  data::Errors errors;
  Region r = parse_region(
      "terrain\n"
      "  size: 100\n"
      "  resolution: 51\n"
      "  noise_height: 0.5\n"
      "  hill h\n"
      "    center: 30 30\n"
      "    radius: 10\n"
      "    height: 8\n"
      "  ridge r\n"
      "    from: -30 0\n"
      "    to: -30 -40\n"
      "    width: 6\n"
      "    height: 5\n"
      "  clearing c\n"
      "    center: 0 0\n"
      "    radius: 5\n"
      "    blend: 5\n"
      "    height: 1\n",
      errors);
  CHECK(errors.ok());
  const Terrain& t = r.terrain;
  CHECK_NEAR(t.height_at(0, 0), 1.0f, 1e-4f);      // clearing is flat at its height
  CHECK_NEAR(t.height_at(3, -2), 1.0f, 1e-4f);
  CHECK(t.height_at(30, 30) > 7.0f);               // hill top
  CHECK(t.height_at(-30, -20) > 4.0f);             // along the ridge
  CHECK(t.normal_at(0, 0).y > 0.999f);             // flat clearing faces straight up
}

TEST(terrain_mesh_faces_up) {
  data::Errors errors;
  Region r = parse_region("terrain\n  size: 20\n  resolution: 5\n", errors);
  Mesh m = build_terrain_mesh(r.terrain);
  CHECK(m.vertices.size() == 25);
  CHECK(m.indices.size() == 4 * 4 * 6);
  for (size_t i = 0; i < m.indices.size(); i += 3) {
    Vec3 a = m.vertices[m.indices[i]].position;
    Vec3 b = m.vertices[m.indices[i + 1]].position;
    Vec3 c = m.vertices[m.indices[i + 2]].position;
    CHECK(cross(b - a, c - a).y > 0.0f);
  }
}

TEST(props_never_float_on_slopes) {
  data::Errors errors;
  Region r = parse_region(
      "terrain\n  size: 100\n  noise_height: 0\n  hill h\n    center: 0 0\n    radius: 20\n    height: 10\n"
      "prop box\n  position: 10 0\n  size: 2 2 2\n  yaw: 30\n",
      errors);
  CHECK(errors.ok());
  Mat4 m = prop_transform(r.terrain, r.props.at(0));
  // The bottom of the box (local y = -0.5) must be at or below the ground under
  // every corner of its footprint.
  for (float sx : {-0.5f, 0.5f}) {
    for (float sz : {-0.5f, 0.5f}) {
      Vec4 corner = m * Vec4{sx, -0.5f, sz, 1.0f};
      CHECK(corner.y <= r.terrain.height_at(corner.x, corner.z) + 1e-3f);
    }
  }
}

TEST(region_reports_unknown_things) {
  data::Errors errors;
  parse_region("terrain\n  mountain m\n    height: 3\nprop p\n  positon: 1 2\n", errors);
  bool mountain = false, positon = false, missing_position = false;
  for (const auto& m : errors.messages) {
    if (m.find("not 'mountain'") != std::string::npos) mountain = true;
    if (m.find("did you mean 'position'") != std::string::npos) positon = true;
    if (m.find("needs a 'position' field") != std::string::npos) missing_position = true;
  }
  CHECK(mountain);
  CHECK(positon);
  CHECK(missing_position);
}

TEST(shipped_region_file_loads_cleanly) {
  data::Errors errors;
  Region r;
  bool ok = load_region(std::string(RK_SOURCE_DIR) + "/content/regions/rung2_forest_lake.rk", r, errors);
  if (!ok) std::printf("  %s\n", errors.joined().c_str());
  CHECK(ok);
  CHECK(!r.props.empty());
  CHECK(!r.terrain.ridges.empty());
}
