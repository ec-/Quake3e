#version 450

layout(set = 0, binding = 0) uniform UBO {
	// VERTEX
	vec4 eyePos;
	vec4 lightPos;
	//  VERTEX-FOG
	vec4 fogDistanceVector;
	vec4 fogDepthVector;
	vec4 fogEyeT;
	// FRAGMENT
	vec4 lightColor;
	vec4 fogColor;
};
layout(set = 1, binding = 0) uniform sampler2D texture0; // diffuse
layout(set = 2, binding = 0) uniform sampler2D texture1; // lightmap
layout(set = 3, binding = 0) uniform sampler2D texture2; // fog

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord;
//layout(location = 2) in vec2 frag_tex_coord1;
layout(location = 3) in vec2 fog_tex_coord; // fog

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const int alpha_test_func = 0;

void main() {
    vec4 base = frag_color * texture(texture0, frag_tex_coord);
	vec4 fog = texture(texture2, fog_tex_coord);

    if (alpha_test_func == 1) {
        if (base.a == 0.0f) discard;
    } else if (alpha_test_func == 2) {
        if (base.a >= 0.5f) discard;
    } else if (alpha_test_func == 3) {
        if (base.a < 0.5f) discard;
    }

	fog = fog * fogColor;

	out_color = mix( base, fog, fog.a );
}
