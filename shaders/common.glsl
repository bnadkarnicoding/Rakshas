// common.glsl — shared declarations for all Rakshas shaders.
// Must match struct SceneConstants in game/scene_constants.h (std140).

layout(set = 0, binding = 0, std140) uniform SceneConstants {
  mat4 view_proj;
  mat4 inv_view_proj;
  mat4 light_view_proj;  // world -> shadow map clip space
  vec4 camera_pos;       // xyz
  vec4 light_dir;        // xyz: toward the active light (sun by day, moon by night)
  vec4 light_color;      // rgb: that light's colour * intensity
  vec4 sun_dir;          // xyz: toward the sun (even when below the horizon)
  vec4 moon_dir;         // xyz: toward the moon; w: night amount 0..1 (stars)
  vec4 sky_zenith;       // rgb: sky colour straight up
  vec4 sky_horizon;      // rgb: sky colour at the horizon (also used for haze)
  vec4 ground_color;     // rgb: light bounced up from the ground
  vec4 params;           // x: haze density, y: exposure, z: time (seconds)
  vec4 shadow_params;    // x: 1 / shadow map size, y: normal offset (m), z: shadow strength
} scene;

// Natural, gentle highlight roll-off. No bloom, no glow (Rule 1.3).
vec3 tonemap(vec3 c) {
  return vec3(1.0) - exp(-c * scene.params.y);
}
