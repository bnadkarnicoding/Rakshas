// game/region.h — a gray-box region loaded from a content file: the shape of
// the land, the placeholder objects on it, and where the camera starts.
#pragma once
#include <string>
#include <vector>

#include "core/data.h"
#include "core/math.h"
#include "shapes.h"

namespace rk {

struct Hill {
  std::string name;
  Vec2 center;
  float radius = 20.0f;
  float height = 5.0f;
};

struct Ridge {
  std::string name;
  Vec2 from, to;
  float width = 10.0f;
  float height = 8.0f;
};

struct Clearing {
  std::string name;
  Vec2 center;
  float radius = 10.0f;
  float blend = 8.0f;     // distance over which the land eases back to normal
  float height = 0.0f;    // flattened ground height
};

struct Terrain {
  float size = 200.0f;          // metres along each side, centred on the origin
  int resolution = 201;         // vertices along each side
  float noise_height = 0.5f;    // gentle unevenness everywhere
  float noise_scale = 20.0f;    // metres between bumps
  int seed = 1;
  Vec3 color{0.42f, 0.34f, 0.24f};
  std::vector<Hill> hills;
  std::vector<Ridge> ridges;
  std::vector<Clearing> clearings;

  float height_at(float x, float z) const;
  Vec3 normal_at(float x, float z) const;
};

enum class PropShape { Box, Figure };

struct PropPlacement {
  std::string name;
  PropShape shape = PropShape::Box;  // Figure = an upright placeholder person (capsule)
  Vec2 position;        // x z on the ground
  Vec3 size{1, 1, 1};
  float yaw = 0.0f;     // degrees
  Vec3 color{0.6f, 0.6f, 0.6f};
};

struct CameraStart {
  Vec2 target;
  float distance = 14.0f;
  float yaw = 35.0f;    // degrees
  float pitch = 20.0f;  // degrees above the horizon
};

// Camera framings a zone can ask for (see camera_director.h).
enum class Framing { Explore, Vista, Duel, Conversation };
const char* framing_name(Framing f);

// A place in the world where the camera changes its framing.
struct CameraZone {
  std::string name;
  Framing framing = Framing::Explore;
  Vec2 center;
  float radius = 10.0f;
  bool has_focus = false;
  Vec2 focus;  // what the framing looks toward: the view, the opponent, the other speaker
};

struct PlayerStart {
  Vec2 position;        // x z
  float facing = 0.0f;  // degrees; 0 faces north (-z), 90 faces west, -90 faces east
};

struct Region {
  Terrain terrain;
  std::vector<PropPlacement> props;
  CameraStart camera;
  PlayerStart player;
  std::vector<CameraZone> camera_zones;
};

// Builds a Region from parsed content. Problems are added to `errors`.
bool build_region(const data::Node& root, Region& out, data::Errors& errors);

// Loads and builds a region file.
bool load_region(const std::string& path, Region& out, data::Errors& errors);

// Triangle mesh for the terrain surface (counter-clockwise, facing up).
Mesh build_terrain_mesh(const Terrain& terrain);

// World transform that rests a box prop on the ground, sunk to its lowest corner
// so it never floats on a slope.
Mat4 prop_transform(const Terrain& terrain, const PropPlacement& prop);

}  // namespace rk
