#version 450

layout(set = 2, binding = 0) uniform sampler2D texture0; // diffuse

layout(location = 0) in vec4 frag_color;
layout(location = 1) centroid in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout (constant_id = 0) const int alpha_test_func = 0;
layout (constant_id = 1) const float alpha_test_value = 0.0;
//layout (constant_id = 2) const float depth_fragment = 0.85;
layout (constant_id = 3) const int alpha_to_coverage = 0;
layout (constant_id = 7) const int discard_mode = 0;

float CorrectAlpha(float threshold, float alpha, vec2 tc)
{
	ivec2 ts = textureSize(texture0, 0);
	float dx = max(abs(dFdx(tc.x * float(ts.x))), 0.001);
	float dy = max(abs(dFdy(tc.y * float(ts.y))), 0.001);
	float dxy = max(dx, dy); // apply the smallest boost
	float scale = max(1.0 / dxy, 1.0);
	float ac = threshold + (alpha - threshold) * scale;
	return ac;
}

void main() {
	vec4 base = texture(texture0, frag_tex_coord) * frag_color;

	if (alpha_to_coverage != 0) {
		if (alpha_test_func == 1) {
			base.a =  base.a > 0.0 ? 1.0 : 0.0;
		} else if (alpha_test_func == 2) {
			base.a = CorrectAlpha(alpha_test_value, 1.0 - base.a, frag_tex_coord);
		} else if (alpha_test_func == 3) {
			base.a = CorrectAlpha(alpha_test_value, base.a, frag_tex_coord);
		}
	} else
	// specialization: alpha-test function
	if (alpha_test_func == 1) {
		if (base.a == alpha_test_value) discard;
	} else if (alpha_test_func == 2) {
		if (base.a >= alpha_test_value) discard;
	} else if (alpha_test_func == 3) {
		if (base.a < alpha_test_value) discard;
	}

	if ( discard_mode == 1 ) {
		if ( base.a == 0.0 ) {
			discard;
		}
	} else if ( discard_mode == 2 ) {
		if ( dot( base.rgb, base.rgb ) == 0.0 ) {
			discard;
		}
	}

	out_color = base;
}
