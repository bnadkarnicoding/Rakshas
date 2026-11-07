// common.glsl — shared declarations for all Rakshas shaders.
// Must match struct SceneConstants in game/scene_constants.h (std140).

layout(set = 0, binding = 0, std140) uniform SceneConstants {
  mat4 view_proj;
  mat4 inv_view_proj;
  vec4 camera_pos;    // xyz
  vec4 sun_dir;       // xyz: unit vector pointing TOWARD the sun (or moon at night)
  vec4 sun_color;     // rgb: light colour * intensity
  vec4 sky_zenith;    // rgb: sky colour straight up
  vec4 sky_horizon;   // rgb: sky colour at the horizon (also used for haze)
  vec4 ground_color;  // rgb: light bounced up from the ground
  vec4 params;        // x: haze density, y: exposure
} scene;

// Natural, gentle highlight roll-off. No bloom, no glow (Rule 1.3).
vec3 tonemap(vec3 c) {
  return vec3(1.0) - exp(-c * scene.params.y);
}
