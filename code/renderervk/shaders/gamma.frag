#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput in_color;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const float gamma = 1.0;
layout(constant_id = 1) const float obScale = 2.0;
layout(constant_id = 2) const float greyscale = 0.0;

const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

void main() {
	vec3 base = subpassLoad(in_color).rgb;

	if ( greyscale == 1 )
	{
		base = vec3(dot(base, sRGB));
	}
	else if ( greyscale != 0 )
	{
		vec3 luma = vec3(dot(base, sRGB));
		base = mix(base, luma, greyscale);
	}

	out_color.rgb = pow(base, vec3(gamma)) * obScale;
}
