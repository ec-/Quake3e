#version 450

layout(set = 2, binding = 0) uniform sampler2D texture0;
layout(set = 3, binding = 0) uniform sampler2D texture1;
//layout(set = 4, binding = 0) uniform sampler2D texture2;

layout(location = 0) in vec4 frag_color0;
layout(location = 1) in vec4 frag_color1;
//layout(location = 2) in vec4 frag_color2;

layout(location = 3) centroid in vec2 frag_tex_coord0;
layout(location = 4) centroid in vec2 frag_tex_coord1;
//layout(location = 5) centroid in vec2 frag_tex_coord2;

layout(location = 0) out vec4 out_color;

//layout (constant_id = 0) const int alpha_test_func = 0;
//layout (constant_id = 1) const float alpha_test_value = 0.0;
//layout (constant_id = 2) const float depth_fragment = 0.85;
//layout (constant_id = 3) const int alpha_to_coverage = 0;
//layout (constant_id = 4) const int color_mode = 0;
//layout (constant_id = 5) const int abs_light = 0;
layout (constant_id = 6) const int tex_mode = 0; // modulate, add (identity), add, alpha, 1-alpha, mix alpha, mix 1-alpha 
layout (constant_id = 7) const int discard_mode = 0;

void main() {
	vec4 base;

	vec4 color0 = texture(texture0, frag_tex_coord0) * frag_color0;
	vec4 color1 = texture(texture1, frag_tex_coord1) * frag_color1;

	if ( tex_mode == 1 || tex_mode == 2 ) {
		// add
		base = vec4( color0.rgb + color1.rgb, color0.a * color1.a );
	} else if ( tex_mode == 3 ) {
		// modulate by alpha
		color0 *= color0.a;
		color1 *= color1.a;
		base = vec4( color0.rgb + color1.rgb, color0.a * color1.a );
	} else if ( tex_mode == 4 ) {
		// modulate by 1.0-alpha
		color0 *= 1.0-color0.a;
		color1 *= 1.0-color1.a;
		base = vec4( color0.rgb + color1.rgb, color0.a * color1.a );
	} else if ( tex_mode == 5 ) {
		// mix by src alpha
		base = mix( color0, color1, color1.a );
	} else if ( tex_mode == 6 ) {
		// mix by 1-src alpha
		base = mix( color1, color0, color1.a );
	} else {
		// modulate
		base = color0 * color1;
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
