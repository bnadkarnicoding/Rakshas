// game/model.h — loading 3D models in glTF 2.0, the format Blender exports
// (File > Export > glTF 2.0, "glTF Separate" so textures stay as PNG files).
#pragma once
#include <string>
#include <vector>

#include "core/data.h"
#include "core/math.h"
#include "shapes.h"

namespace rk {

struct ModelPart {
  std::string name;
  Mesh mesh;                 // already in the model's own space (node transforms applied)
  std::string texture_path;  // full path to the base-colour image, or empty
  Vec3 color{1, 1, 1};       // base-colour factor (multiplies the texture)
};

struct Model {
  std::vector<ModelPart> parts;
  Vec3 bounds_min, bounds_max;
};

// Loads a .gltf file (with its .bin and image files). Problems go to `errors`
// in plain words, e.g. a missing texture file.
bool load_model(const std::string& path, Model& out, data::Errors& errors);

// Loads a PNG/JPG as tightly packed RGBA8.
bool load_image_rgba(const std::string& path, std::vector<u8>& rgba, u32& width, u32& height);

}  // namespace rk
