#version 450

layout(set = 1, binding = 0) uniform sampler2D texture0; // diffuse

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord;

//layout(location = 0) out vec4 out_color;
//layout(location = 1) out float fragDepth;
layout(depth_unchanged) out float gl_FragDepth;

layout (constant_id = 0) const int alpha_test_func = 0;
layout (constant_id = 1) const float alpha_test_value = 0.0;
layout (constant_id = 2) const float depth_fragment = 0.85;

void main() {
	vec4 base = frag_color * texture(texture0, frag_tex_coord);

	if (alpha_test_func == 1) {
		if (base.a == alpha_test_value) discard;
	} else if (alpha_test_func == 2) {
		if (base.a >= alpha_test_value) discard;
	} else if (alpha_test_func == 3) {
		if (base.a < alpha_test_value) discard;
	}

	if (base.a < depth_fragment) discard;

	gl_FragDepth = gl_FragCoord.z;
}
