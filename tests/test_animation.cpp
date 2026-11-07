// tests/test_animation.cpp — skeleton loading, sampling, blending and skinning.
#include "animation.h"
#include "test.h"

using namespace rk;

static const SkinnedModel& mannequin() {
  static SkinnedModel model;
  static bool loaded = false;
  if (!loaded) {
    data::Errors errors;
    if (!load_skinned_model(std::string(RK_SOURCE_DIR) + "/content/models/mannequin.gltf", model, errors))
      std::printf("  %s\n", errors.joined().c_str());
    loaded = true;
  }
  return model;
}

static Vec3 joint_position(const SkinnedModel& m, const LocalPose& pose, const char* name) {
  std::vector<Mat4> g = joint_globals(m.skeleton, pose);
  const Mat4& j = g[static_cast<size_t>(m.skeleton.find(name))];
  return {j.at(3, 0), j.at(3, 1), j.at(3, 2)};
}

TEST(mannequin_has_skeleton_and_clips) {
  const SkinnedModel& m = mannequin();
  CHECK(m.skeleton.joints.size() == 17);
  CHECK(m.skeleton.order.size() == 17);
  CHECK(m.skeleton.find("hips") >= 0 && m.skeleton.joints[static_cast<size_t>(m.skeleton.find("hips"))].parent == -1);
  CHECK(m.skeleton.joints[static_cast<size_t>(m.skeleton.find("shin_l"))].parent == m.skeleton.find("thigh_l"));
  CHECK(m.clip("idle") && m.clip("walk") && m.clip("run"));
  CHECK_NEAR(m.clip("walk")->duration, 1.0f, 1e-4f);
  CHECK_NEAR(m.clip("run")->duration, 0.66f, 1e-4f);
  CHECK(m.vertices.size() > 1000);
}

TEST(rest_pose_skins_to_the_original_mesh) {
  const SkinnedModel& m = mannequin();
  std::vector<Mat4> skin = skin_matrices(m.skeleton, rest_pose(m.skeleton));
  std::vector<Vertex> out;
  skin_vertices(m, skin, out);
  float worst = 0.0f;
  for (size_t i = 0; i < out.size(); ++i) worst = std::fmax(worst, length(out[i].position - m.vertices[i].position));
  CHECK(worst < 1e-4f);
}

TEST(walk_swings_legs_forward_and_back) {
  const SkinnedModel& m = mannequin();
  LocalPose pose = rest_pose(m.skeleton);
  sample_clip(*m.clip("walk"), 0.25f, pose);  // left leg furthest forward
  Vec3 left = joint_position(m, pose, "foot_l");
  Vec3 right = joint_position(m, pose, "foot_r");
  CHECK(left.z < -0.15f);   // forward is -Z
  CHECK(right.z > 0.15f);
  // Half a cycle later the legs have swapped.
  sample_clip(*m.clip("walk"), 0.75f, pose);
  CHECK(joint_position(m, pose, "foot_l").z > 0.15f);
}

TEST(clips_loop_seamlessly) {
  const SkinnedModel& m = mannequin();
  for (const char* name : {"idle", "walk", "run"}) {
    const AnimationClip& c = *m.clip(name);
    LocalPose a = rest_pose(m.skeleton), b = rest_pose(m.skeleton);
    sample_clip(c, 0.0f, a);
    sample_clip(c, c.duration * 3.0f + 1e-4f, b);  // three loops later
    Vec3 fa = joint_position(m, a, "hand_r"), fb = joint_position(m, b, "hand_r");
    CHECK(length(fa - fb) < 0.01f);
  }
}

TEST(blending_goes_smoothly_between_poses) {
  const SkinnedModel& m = mannequin();
  LocalPose idle = rest_pose(m.skeleton), run = rest_pose(m.skeleton);
  sample_clip(*m.clip("idle"), 0.0f, idle);
  sample_clip(*m.clip("run"), 0.165f, run);
  Vec3 a = joint_position(m, blend_poses(idle, run, 0.0f), "foot_l");
  Vec3 b = joint_position(m, blend_poses(idle, run, 1.0f), "foot_l");
  Vec3 mid = joint_position(m, blend_poses(idle, run, 0.5f), "foot_l");
  CHECK(length(a - joint_position(m, idle, "foot_l")) < 1e-4f);
  CHECK(length(b - joint_position(m, run, "foot_l")) < 1e-4f);
  CHECK(mid.z < a.z && mid.z > b.z);  // halfway between standing and the running stride
}

TEST(stride_keeps_pace_with_speed) {
  const SkinnedModel& m = mannequin();
  LocomotionAnimator walker(&m), runner(&m);
  for (int i = 0; i < 60; ++i) {  // one second
    walker.update(2.0f, 2.0f, 5.0f, 1.0f / 60.0f);
    runner.update(5.0f, 2.0f, 5.0f, 1.0f / 60.0f);
  }
  CHECK(walker.move_blend() > 0.99f && walker.run_blend() < 0.01f);
  CHECK(runner.run_blend() > 0.99f);
  // 2 m/s for 1 s over a 2 m stride = exactly one cycle (phase back near 0).
  CHECK(std::fmin(walker.phase(), 1.0f - walker.phase()) < 0.02f);
  LocomotionAnimator still(&m);
  for (int i = 0; i < 60; ++i) still.update(0.0f, 2.0f, 5.0f, 1.0f / 60.0f);
  CHECK(still.move_blend() < 1e-3f);
}
