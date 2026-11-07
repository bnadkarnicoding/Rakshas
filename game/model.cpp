// game/model.cpp — glTF loading with cgltf, images with stb_image (both approved).
#include "model.h"

#include <algorithm>
#include <cstdio>

// Third-party code is compiled without our strict warning set.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "core/file.h"

namespace rk {
namespace {

std::string folder_of(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

bool file_exists(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

Vec3 transform_point(const float m[16], Vec3 p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

Vec3 transform_direction(const float m[16], Vec3 d) {
  return normalize(Vec3{m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
                        m[2] * d.x + m[6] * d.y + m[10] * d.z});
}

void add_primitive(const cgltf_primitive& prim, const float world[16], const std::string& folder,
                   const std::string& mesh_name, const std::string& file, Model& out,
                   data::Errors& errors) {
  if (prim.type != cgltf_primitive_type_triangles) {
    errors.add(file, 1, "mesh '" + mesh_name + "' has a part that isn't made of triangles (skipped)");
    return;
  }
  const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
  for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
    const cgltf_attribute& a = prim.attributes[i];
    if (a.type == cgltf_attribute_type_position) pos = a.data;
    if (a.type == cgltf_attribute_type_normal) nrm = a.data;
    if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
  }
  if (!pos) {
    errors.add(file, 1, "mesh '" + mesh_name + "' has a part with no positions (skipped)");
    return;
  }

  ModelPart part;
  part.name = mesh_name;
  part.mesh.vertices.resize(pos->count);
  for (cgltf_size i = 0; i < pos->count; ++i) {
    float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0};
    cgltf_accessor_read_float(pos, i, p, 3);
    if (nrm) cgltf_accessor_read_float(nrm, i, n, 3);
    if (uv) cgltf_accessor_read_float(uv, i, t, 2);
    part.mesh.vertices[i] = {transform_point(world, {p[0], p[1], p[2]}),
                             transform_direction(world, {n[0], n[1], n[2]}), {t[0], t[1]}};
  }
  if (prim.indices) {
    part.mesh.indices.resize(prim.indices->count);
    for (cgltf_size i = 0; i < prim.indices->count; ++i)
      part.mesh.indices[i] = static_cast<u32>(cgltf_accessor_read_index(prim.indices, i));
  } else {
    for (cgltf_size i = 0; i < pos->count; ++i) part.mesh.indices.push_back(static_cast<u32>(i));
  }
  if (!nrm) errors.add(file, 1, "mesh '" + mesh_name + "' has no normals; export with normals enabled");

  if (prim.material) {
    const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
    part.color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
    const cgltf_texture* tex = pbr.base_color_texture.texture;
    if (tex && tex->image) {
      if (tex->image->uri) {
        part.texture_path = folder + tex->image->uri;
        if (!file_exists(part.texture_path)) {
          errors.add(file, 1, "texture '" + std::string(tex->image->uri) + "' was not found next to the model");
          part.texture_path.clear();
        }
      } else {
        errors.add(file, 1, "textures packed inside the model aren't supported yet; export as 'glTF Separate'");
      }
    }
  }
  out.parts.push_back(std::move(part));
}

}  // namespace

bool load_model(const std::string& path, Model& out, data::Errors& errors) {
  size_t before = errors.messages.size();
  out = Model{};
  cgltf_options options{};
  cgltf_data* gltf = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &gltf) != cgltf_result_success) {
    errors.add(path, 1, "could not read this model (is it a glTF 2.0 file?)");
    return false;
  }
  if (cgltf_load_buffers(&options, gltf, path.c_str()) != cgltf_result_success) {
    errors.add(path, 1, "could not load the model's data (.bin) file");
    cgltf_free(gltf);
    return false;
  }
  if (cgltf_validate(gltf) != cgltf_result_success) {
    errors.add(path, 1, "the model file is damaged or incomplete");
    cgltf_free(gltf);
    return false;
  }

  const std::string folder = folder_of(path);
  for (cgltf_size n = 0; n < gltf->nodes_count; ++n) {
    const cgltf_node& node = gltf->nodes[n];
    if (!node.mesh) continue;
    float world[16];
    cgltf_node_transform_world(&node, world);
    std::string mesh_name = node.mesh->name ? node.mesh->name : (node.name ? node.name : "mesh");
    for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
      add_primitive(node.mesh->primitives[p], world, folder, mesh_name, path, out, errors);
  }
  cgltf_free(gltf);

  bool first = true;
  for (const ModelPart& part : out.parts) {
    for (const Vertex& v : part.mesh.vertices) {
      if (first) {
        out.bounds_min = out.bounds_max = v.position;
        first = false;
      }
      out.bounds_min = {std::min(out.bounds_min.x, v.position.x), std::min(out.bounds_min.y, v.position.y),
                        std::min(out.bounds_min.z, v.position.z)};
      out.bounds_max = {std::max(out.bounds_max.x, v.position.x), std::max(out.bounds_max.y, v.position.y),
                        std::max(out.bounds_max.z, v.position.z)};
    }
  }
  if (out.parts.empty()) errors.add(path, 1, "the model contains no meshes");
  return errors.messages.size() == before && !out.parts.empty();
}

bool load_image_rgba(const std::string& path, std::vector<u8>& rgba, u32& width, u32& height) {
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
  if (!pixels) return false;
  width = static_cast<u32>(w);
  height = static_cast<u32>(h);
  rgba.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  stbi_image_free(pixels);
  return true;
}

}  // namespace rk
