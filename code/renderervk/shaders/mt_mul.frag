#version 450

// layout set = 1 - uniform
layout(set = 1, binding = 0) uniform sampler2D texture1;
layout(set = 2, binding = 0) uniform sampler2D texture2;

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord1;
layout(location = 2) in vec2 frag_tex_coord2;

layout(location = 0) out vec4 out_color;

layout (constant_id = 0) const int alpha_test_func = 0;
layout (constant_id = 1) const float alpha_test_value = 0.0;
//layout (constant_id = 2) const float depth_fragment = 0.85;
layout (constant_id = 3) const int alpha_to_coverage = 0;

float CorrectAlpha(float threshold, float alpha, vec2 tc)
{
	ivec2 ts = textureSize(texture1, 0);
	float dx = max(dFdx(tc.x * float(ts.x)), 0.001);
	float dy = max(dFdy(tc.y * float(ts.y)), 0.001);
	float dxy = max(dx, dy); // apply the smallest boost
	float scale = max(1.0 / dxy, 1.0);
	float ac = threshold + (alpha - threshold) * scale;
	return ac;
}

void main() {
	out_color = frag_color * texture(texture1, frag_tex_coord1) * texture(texture2, frag_tex_coord2);

	if (alpha_to_coverage != 0) {
		if (alpha_test_func == 1) {
			out_color.a = CorrectAlpha(alpha_test_value, out_color.a, frag_tex_coord1);
		} else if (alpha_test_func == 2) {
			out_color.a = CorrectAlpha(alpha_test_value, 1.0 - out_color.a, frag_tex_coord1);
		} else if (alpha_test_func == 3) {
			out_color.a = CorrectAlpha(alpha_test_value, out_color.a, frag_tex_coord1);
		}
	} else
	// specialization: alpha-test function
	if (alpha_test_func == 1) {
		if (out_color.a == alpha_test_value) discard;
	} else if (alpha_test_func == 2) {
		if (out_color.a >= alpha_test_value) discard;
	} else if (alpha_test_func == 3) {
		if (out_color.a < alpha_test_value) discard;
	}
}
