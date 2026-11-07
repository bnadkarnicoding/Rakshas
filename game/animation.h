// game/animation.h — skeletal animation: skeletons, clips, sampling, blending
// and skinning, loaded from glTF (the format Blender exports).
//
// Skinning is done on the CPU for now (a handful of characters is cheap). It
// moves to the GPU when crowds arrive; the data here stays the same.
#pragma once
#include <string>
#include <vector>

#include "core/data.h"
#include "core/math.h"
#include "shapes.h"

namespace rk {

struct Joint {
  std::string name;
  int parent = -1;   // index into Skeleton::joints, -1 for the root
  Vec3 rest_translation;
  Quat rest_rotation;
  Vec3 rest_scale{1, 1, 1};
  Mat4 inverse_bind = Mat4::identity();
};

struct Skeleton {
  std::vector<Joint> joints;
  std::vector<int> order;  // parents always come before their children
  int find(const std::string& name) const;
};

struct AnimationChannel {
  enum class Path { Translation, Rotation, Scale };
  int joint = 0;
  Path path = Path::Rotation;
  bool step = false;            // true = hold values (no interpolation)
  std::vector<float> times;     // seconds, increasing
  std::vector<Vec4> values;     // xyz (translation/scale) or xyzw (rotation)
};

struct AnimationClip {
  std::string name;
  float duration = 0.0f;
  std::vector<AnimationChannel> channels;
};

// Each joint's position, rotation and scale relative to its parent.
struct LocalPose {
  std::vector<Vec3> translation;
  std::vector<Quat> rotation;
  std::vector<Vec3> scale;
};

struct SkinnedVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
  u16 joints[4] = {0, 0, 0, 0};
  float weights[4] = {1, 0, 0, 0};
};

struct SkinnedModel {
  Skeleton skeleton;
  std::vector<AnimationClip> clips;
  std::vector<SkinnedVertex> vertices;
  std::vector<u32> indices;
  Vec3 color{1, 1, 1};
  std::string texture_path;

  const AnimationClip* clip(const std::string& name) const;
};

bool load_skinned_model(const std::string& path, SkinnedModel& out, data::Errors& errors);

LocalPose rest_pose(const Skeleton& skeleton);

// Overwrites the joints the clip animates; `time` loops over the clip's length.
void sample_clip(const AnimationClip& clip, float time, LocalPose& pose);

// out = a when t = 0, b when t = 1.
LocalPose blend_poses(const LocalPose& a, const LocalPose& b, float t);

// Model-space transform of every joint.
std::vector<Mat4> joint_globals(const Skeleton& skeleton, const LocalPose& pose);

// Matrices that move each vertex from its rest position to the posed one.
std::vector<Mat4> skin_matrices(const Skeleton& skeleton, const LocalPose& pose);

// Writes the posed mesh into `out` (same vertex order as the model).
void skin_vertices(const SkinnedModel& model, const std::vector<Mat4>& skin, std::vector<Vertex>& out);

// Chooses and blends idle / walk / run from how fast a character is moving,
// keeping the stride in step with the speed so feet don't slide.
class LocomotionAnimator {
 public:
  explicit LocomotionAnimator(const SkinnedModel* model = nullptr, float time_offset = 0.0f)
      : model_(model), idle_time_(time_offset) {}

  // `speed` in m/s; walk/run speeds are the character's own.
  void update(float speed, float walk_speed, float run_speed, float dt);
  LocalPose pose() const;

  float phase() const { return phase_; }  // 0..1 through the stride cycle
  float move_blend() const { return move_; }
  float run_blend() const { return run_; }

 private:
  const SkinnedModel* model_;
  float idle_time_ = 0.0f;
  float phase_ = 0.0f;
  float move_ = 0.0f;
  float run_ = 0.0f;
};

// Metres covered by one full walk or run cycle (two steps). Must match the
// clips: walk = 1.0 s at 2 m/s, run = 0.66 s at 5 m/s.
constexpr float kWalkStride = 2.0f;
constexpr float kRunStride = 3.3f;

}  // namespace rk
