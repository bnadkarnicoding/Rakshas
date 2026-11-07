// game/animation.cpp — skeletal animation (glTF via cgltf, approved list).
#include "animation.h"

#include <algorithm>
#include <cstdio>

#include "cgltf/cgltf.h"  // implementation lives in model.cpp

namespace rk {
namespace {

Vec4 lerp4(Vec4 a, Vec4 b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

Vec4 sample_channel(const AnimationChannel& c, float t) {
  if (c.times.empty()) return {};
  if (t <= c.times.front()) return c.values.front();
  if (t >= c.times.back()) return c.values.back();
  size_t hi = static_cast<size_t>(std::upper_bound(c.times.begin(), c.times.end(), t) - c.times.begin());
  size_t lo = hi - 1;
  if (c.step) return c.values[lo];
  float span = c.times[hi] - c.times[lo];
  float f = span > 0.0f ? (t - c.times[lo]) / span : 0.0f;
  if (c.path == AnimationChannel::Path::Rotation) {
    Quat q = nlerp({c.values[lo].x, c.values[lo].y, c.values[lo].z, c.values[lo].w},
                   {c.values[hi].x, c.values[hi].y, c.values[hi].z, c.values[hi].w}, f);
    return {q.x, q.y, q.z, q.w};
  }
  return lerp4(c.values[lo], c.values[hi], f);
}

Vec3 v3(Vec4 v) { return {v.x, v.y, v.z}; }

}  // namespace

int Skeleton::find(const std::string& name) const {
  for (size_t i = 0; i < joints.size(); ++i)
    if (joints[i].name == name) return static_cast<int>(i);
  return -1;
}

const AnimationClip* SkinnedModel::clip(const std::string& name) const {
  for (const AnimationClip& c : clips)
    if (c.name == name) return &c;
  return nullptr;
}

bool load_skinned_model(const std::string& path, SkinnedModel& out, data::Errors& errors) {
  size_t before = errors.messages.size();
  out = SkinnedModel{};
  cgltf_options options{};
  cgltf_data* gltf = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &gltf) != cgltf_result_success) {
    errors.add(path, 1, "could not read this model (is it a glTF 2.0 file?)");
    return false;
  }
  if (cgltf_load_buffers(&options, gltf, path.c_str()) != cgltf_result_success ||
      cgltf_validate(gltf) != cgltf_result_success) {
    errors.add(path, 1, "the model's data is missing or damaged");
    cgltf_free(gltf);
    return false;
  }
  if (gltf->skins_count == 0) {
    errors.add(path, 1, "this model has no skeleton (export it with its armature)");
    cgltf_free(gltf);
    return false;
  }

  // Skeleton.
  const cgltf_skin& skin = gltf->skins[0];
  Skeleton& sk = out.skeleton;
  sk.joints.resize(skin.joints_count);
  auto joint_of = [&](const cgltf_node* node) -> int {
    for (cgltf_size i = 0; i < skin.joints_count; ++i)
      if (skin.joints[i] == node) return static_cast<int>(i);
    return -1;
  };
  for (cgltf_size i = 0; i < skin.joints_count; ++i) {
    const cgltf_node* n = skin.joints[i];
    Joint& j = sk.joints[i];
    j.name = n->name ? n->name : "joint" + std::to_string(i);
    j.parent = n->parent ? joint_of(n->parent) : -1;
    if (n->has_translation) j.rest_translation = {n->translation[0], n->translation[1], n->translation[2]};
    if (n->has_rotation) j.rest_rotation = {n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
    if (n->has_scale) j.rest_scale = {n->scale[0], n->scale[1], n->scale[2]};
    if (skin.inverse_bind_matrices) cgltf_accessor_read_float(skin.inverse_bind_matrices, i, j.inverse_bind.m, 16);
  }
  // Order joints so that each parent is handled before its children.
  std::vector<bool> placed(sk.joints.size(), false);
  while (sk.order.size() < sk.joints.size()) {
    size_t before_pass = sk.order.size();
    for (size_t i = 0; i < sk.joints.size(); ++i) {
      int p = sk.joints[i].parent;
      if (!placed[i] && (p < 0 || placed[static_cast<size_t>(p)])) {
        placed[i] = true;
        sk.order.push_back(static_cast<int>(i));
      }
    }
    if (sk.order.size() == before_pass) break;  // malformed hierarchy
  }

  // Mesh: the first mesh that uses this skin.
  const cgltf_primitive* prim = nullptr;
  for (cgltf_size n = 0; n < gltf->nodes_count && !prim; ++n)
    if (gltf->nodes[n].skin == &skin && gltf->nodes[n].mesh && gltf->nodes[n].mesh->primitives_count)
      prim = &gltf->nodes[n].mesh->primitives[0];
  if (!prim) {
    errors.add(path, 1, "the skeleton isn't attached to any mesh");
  } else {
    const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *jnt = nullptr, *wgt = nullptr;
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
      const cgltf_attribute& a = prim->attributes[i];
      if (a.type == cgltf_attribute_type_position) pos = a.data;
      if (a.type == cgltf_attribute_type_normal) nrm = a.data;
      if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
      if (a.type == cgltf_attribute_type_joints && a.index == 0) jnt = a.data;
      if (a.type == cgltf_attribute_type_weights && a.index == 0) wgt = a.data;
    }
    if (!pos || !jnt || !wgt) {
      errors.add(path, 1, "the mesh is missing positions or skin weights");
    } else {
      out.vertices.resize(pos->count);
      for (cgltf_size i = 0; i < pos->count; ++i) {
        float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, t[2] = {0, 0}, w[4] = {1, 0, 0, 0};
        cgltf_uint js[4] = {0, 0, 0, 0};
        cgltf_accessor_read_float(pos, i, p, 3);
        if (nrm) cgltf_accessor_read_float(nrm, i, n, 3);
        if (uv) cgltf_accessor_read_float(uv, i, t, 2);
        cgltf_accessor_read_uint(jnt, i, js, 4);
        cgltf_accessor_read_float(wgt, i, w, 4);
        SkinnedVertex& v = out.vertices[i];
        v.position = {p[0], p[1], p[2]};
        v.normal = {n[0], n[1], n[2]};
        v.uv = {t[0], t[1]};
        float total = w[0] + w[1] + w[2] + w[3];
        for (int k = 0; k < 4; ++k) {
          v.joints[k] = static_cast<u16>(std::min<cgltf_uint>(js[k], static_cast<cgltf_uint>(sk.joints.size() - 1)));
          v.weights[k] = total > 0 ? w[k] / total : (k == 0 ? 1.0f : 0.0f);
        }
      }
      if (prim->indices) {
        for (cgltf_size i = 0; i < prim->indices->count; ++i)
          out.indices.push_back(static_cast<u32>(cgltf_accessor_read_index(prim->indices, i)));
      } else {
        for (cgltf_size i = 0; i < pos->count; ++i) out.indices.push_back(static_cast<u32>(i));
      }
    }
    if (prim->material) {
      const auto& pbr = prim->material->pbr_metallic_roughness;
      out.color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
      const cgltf_texture* tex = pbr.base_color_texture.texture;
      if (tex && tex->image && tex->image->uri) {
        size_t slash = path.find_last_of("/\\");
        out.texture_path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + tex->image->uri;
      }
    }
  }

  // Animations.
  for (cgltf_size a = 0; a < gltf->animations_count; ++a) {
    const cgltf_animation& anim = gltf->animations[a];
    AnimationClip clip;
    clip.name = anim.name ? anim.name : "animation" + std::to_string(a);
    for (cgltf_size c = 0; c < anim.channels_count; ++c) {
      const cgltf_animation_channel& ch = anim.channels[c];
      int joint = joint_of(ch.target_node);
      if (joint < 0) continue;
      AnimationChannel out_ch;
      out_ch.joint = joint;
      int width = 3;
      if (ch.target_path == cgltf_animation_path_type_translation) {
        out_ch.path = AnimationChannel::Path::Translation;
      } else if (ch.target_path == cgltf_animation_path_type_rotation) {
        out_ch.path = AnimationChannel::Path::Rotation;
        width = 4;
      } else if (ch.target_path == cgltf_animation_path_type_scale) {
        out_ch.path = AnimationChannel::Path::Scale;
      } else {
        continue;  // morph weights are not supported
      }
      out_ch.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
      if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline)
        errors.add(path, 1, "animation '" + clip.name + "' uses cubic-spline keys; export with Linear interpolation");
      const cgltf_accessor* in = ch.sampler->input;
      const cgltf_accessor* val = ch.sampler->output;
      for (cgltf_size k = 0; k < in->count; ++k) {
        float t = 0;
        cgltf_accessor_read_float(in, k, &t, 1);
        float v[4] = {0, 0, 0, 1};
        cgltf_accessor_read_float(val, k, v, static_cast<cgltf_size>(width));
        out_ch.times.push_back(t);
        out_ch.values.push_back({v[0], v[1], v[2], width == 4 ? v[3] : 0.0f});
        clip.duration = std::max(clip.duration, t);
      }
      clip.channels.push_back(std::move(out_ch));
    }
    out.clips.push_back(std::move(clip));
  }
  cgltf_free(gltf);
  return errors.messages.size() == before && !out.vertices.empty();
}

LocalPose rest_pose(const Skeleton& skeleton) {
  LocalPose p;
  for (const Joint& j : skeleton.joints) {
    p.translation.push_back(j.rest_translation);
    p.rotation.push_back(j.rest_rotation);
    p.scale.push_back(j.rest_scale);
  }
  return p;
}

void sample_clip(const AnimationClip& clip, float time, LocalPose& pose) {
  float t = clip.duration > 0.0f ? time - clip.duration * std::floor(time / clip.duration) : 0.0f;
  for (const AnimationChannel& c : clip.channels) {
    size_t j = static_cast<size_t>(c.joint);
    if (j >= pose.rotation.size()) continue;
    Vec4 v = sample_channel(c, t);
    switch (c.path) {
      case AnimationChannel::Path::Translation: pose.translation[j] = v3(v); break;
      case AnimationChannel::Path::Rotation: pose.rotation[j] = normalize(Quat{v.x, v.y, v.z, v.w}); break;
      case AnimationChannel::Path::Scale: pose.scale[j] = v3(v); break;
    }
  }
}

LocalPose blend_poses(const LocalPose& a, const LocalPose& b, float t) {
  LocalPose out = a;
  for (size_t i = 0; i < a.rotation.size() && i < b.rotation.size(); ++i) {
    out.translation[i] = lerp(a.translation[i], b.translation[i], t);
    out.rotation[i] = nlerp(a.rotation[i], b.rotation[i], t);
    out.scale[i] = lerp(a.scale[i], b.scale[i], t);
  }
  return out;
}

std::vector<Mat4> joint_globals(const Skeleton& skeleton, const LocalPose& pose) {
  std::vector<Mat4> global(skeleton.joints.size(), Mat4::identity());
  for (int i : skeleton.order) {
    size_t u = static_cast<size_t>(i);
    Mat4 local = compose_trs(pose.translation[u], pose.rotation[u], pose.scale[u]);
    int p = skeleton.joints[u].parent;
    global[u] = p < 0 ? local : global[static_cast<size_t>(p)] * local;
  }
  return global;
}

std::vector<Mat4> skin_matrices(const Skeleton& skeleton, const LocalPose& pose) {
  std::vector<Mat4> m = joint_globals(skeleton, pose);
  for (size_t i = 0; i < m.size(); ++i) m[i] = m[i] * skeleton.joints[i].inverse_bind;
  return m;
}

void skin_vertices(const SkinnedModel& model, const std::vector<Mat4>& skin, std::vector<Vertex>& out) {
  out.resize(model.vertices.size());
  for (size_t i = 0; i < model.vertices.size(); ++i) {
    const SkinnedVertex& v = model.vertices[i];
    Vec3 p{}, n{};
    for (int k = 0; k < 4; ++k) {
      float w = v.weights[k];
      if (w <= 0.0f) continue;
      const Mat4& m = skin[v.joints[k]];
      Vec4 pp = m * Vec4{v.position.x, v.position.y, v.position.z, 1.0f};
      Vec4 nn = m * Vec4{v.normal.x, v.normal.y, v.normal.z, 0.0f};
      p = p + Vec3{pp.x, pp.y, pp.z} * w;
      n = n + Vec3{nn.x, nn.y, nn.z} * w;
    }
    out[i] = {p, normalize(n), v.uv};
  }
}

void LocomotionAnimator::update(float speed, float walk_speed, float run_speed, float dt) {
  float want_move = clampf(speed / std::max(walk_speed * 0.5f, 0.01f), 0.0f, 1.0f);
  float want_run = clampf((speed - walk_speed) / std::max(run_speed - walk_speed, 0.01f), 0.0f, 1.0f);
  float k = 1.0f - std::exp(-10.0f * dt);  // blends settle in about 0.3 s
  move_ += (want_move - move_) * k;
  run_ += (want_run - run_) * k;
  float stride = lerpf(kWalkStride, kRunStride, run_);
  phase_ += speed / stride * dt;
  phase_ -= std::floor(phase_);
  idle_time_ += dt;
}

LocalPose LocomotionAnimator::pose() const {
  if (!model_) return {};
  LocalPose idle = rest_pose(model_->skeleton);
  if (const AnimationClip* c = model_->clip("idle")) sample_clip(*c, idle_time_, idle);
  if (move_ < 1e-3f) return idle;
  LocalPose walk = idle, run = idle;
  if (const AnimationClip* c = model_->clip("walk")) sample_clip(*c, phase_ * c->duration, walk);
  if (const AnimationClip* c = model_->clip("run")) sample_clip(*c, phase_ * c->duration, run);
  return blend_poses(idle, blend_poses(walk, run, run_), move_);
}

}  // namespace rk
