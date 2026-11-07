// tests/test_model.cpp — loading the placeholder hut (glTF) and its textures.
#include "model.h"
#include "test.h"

using namespace rk;

TEST(hut_model_loads_with_textures) {
  Model m;
  data::Errors errors;
  bool ok = load_model(std::string(RK_SOURCE_DIR) + "/content/models/hut_round.gltf", m, errors);
  if (!ok) std::printf("  %s\n", errors.joined().c_str());
  CHECK(ok);
  CHECK(m.parts.size() == 2);
  size_t triangles = 0;
  for (const ModelPart& p : m.parts) {
    CHECK(!p.texture_path.empty());
    triangles += p.mesh.indices.size() / 3;
  }
  CHECK(triangles > 300);
  // Roughly a 5 m wide, 3.8 m tall hut standing on the ground.
  CHECK_NEAR(m.bounds_min.y, 0.0f, 1e-3f);
  CHECK(m.bounds_max.y > 3.5f && m.bounds_max.y < 4.2f);
  CHECK(m.bounds_max.x > 2.3f && m.bounds_max.x < 2.8f);
}

TEST(hut_model_faces_point_outward) {
  Model m;
  data::Errors errors;
  load_model(std::string(RK_SOURCE_DIR) + "/content/models/hut_round.gltf", m, errors);
  int bad = 0, total = 0;
  for (const ModelPart& p : m.parts) {
    for (size_t i = 0; i + 2 < p.mesh.indices.size(); i += 3) {
      const Vertex& a = p.mesh.vertices[p.mesh.indices[i]];
      const Vertex& b = p.mesh.vertices[p.mesh.indices[i + 1]];
      const Vertex& c = p.mesh.vertices[p.mesh.indices[i + 2]];
      Vec3 face = cross(b.position - a.position, c.position - a.position);
      if (length(face) < 1e-8f) continue;
      ++total;
      if (dot(face, a.normal + b.normal + c.normal) <= 0.0f) ++bad;
    }
  }
  CHECK(total > 300);
  CHECK(bad == 0);
}

TEST(missing_model_reports_clearly) {
  Model m;
  data::Errors errors;
  CHECK(!load_model("/no/such/hut.gltf", m, errors));
  CHECK(!errors.ok());
}

TEST(png_textures_load) {
  std::vector<u8> rgba;
  u32 w = 0, h = 0;
  CHECK(load_image_rgba(std::string(RK_SOURCE_DIR) + "/content/textures/earth.png", rgba, w, h));
  CHECK(w == 512 && h == 512);
  CHECK(rgba.size() == 512u * 512u * 4u);
}
