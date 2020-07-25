#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

//layout(constant_id = 0) const float gamma = 1.0;
//layout(constant_id = 1) const float obScale = 2.0;
//layout(constant_id = 2) const float greyscale = 0.0;
layout(constant_id = 3) const float threshold = 0.6;
//layout(constant_id = 4) const float factor = 0.5;

//const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;

	if ( base.r >= threshold || base.g >= threshold || base.b >= threshold )
	{
		out_color = vec4( base, 1.0 );
	}
	else
	{
		out_color = vec4( 0.0 );
	}
}
