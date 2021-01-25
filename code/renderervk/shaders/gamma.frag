#version 450

layout(set = 0, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(constant_id = 0) const float gamma = 1.0;
layout(constant_id = 1) const float obScale = 2.0;
layout(constant_id = 2) const float greyscale = 0.0;

const vec3 sRGB = { 0.2126, 0.7152, 0.0722 };

const ivec3 depth = { 255, 255, 255 };
const int bayerSize = 8;
const float bayerMatrix[bayerSize * bayerSize] = {
	0,  32, 8,  40, 2,  34, 10, 42,
	48, 16, 56, 24, 50, 18, 58, 26,
	12, 44, 4,  36, 14, 46, 6,  38,
	60, 28, 52, 20, 62, 30, 54, 22,
	3,  35, 11, 43, 1,  33, 9,  41,
	51, 19, 59, 27, 49, 17, 57, 25,
	15, 47, 7,  39, 13, 45, 5,  37,
	63, 31, 55, 23, 61, 29, 53, 21
};
const ivec2 screenSize = { 2560, 1440 };

float threshold(vec2 coord) {
	ivec2 coordDenormalized = ivec2(coord * screenSize);
	ivec2 bayerCoord = (coordDenormalized/* + offset*/) % bayerSize;
	/*
	if (rotation == 0) {
		bayerCoord = int2(bayerCoord.x, bayerCoord.y);
	} else if (rotation == 1) {
		bayerCoord = int2(bayerCoord.y, bayerSize - 1 - bayerCoord.x);
	} else if (rotation == 2) {
		bayerCoord = int2(bayerSize - 1 - bayerCoord.x, bayerSize - 1 - bayerCoord.y);
	} else if (rotation == 3) {
		bayerCoord = int2(bayerSize - 1 - bayerCoord.y, bayerCoord.x);
	}
	*/

	float bayerSample = bayerMatrix[bayerCoord.x + bayerCoord.y * bayerSize];
	float threshold = (bayerSample + 1.0) / float(bayerSize * bayerSize);
	return threshold;
}

vec3 dither(vec3 color, vec2 coord)
{
	vec3 cDenormalized = color * depth;
	vec3 cFractional = fract(cDenormalized);
	vec3 cLow = cDenormalized - cFractional;
	vec3 cDithered = cLow + step(threshold(coord), cFractional);
	return cDithered / depth;
}

void main() {
	vec3 base = texture(texture0, frag_tex_coord).rgb;

	if ( greyscale == 1 )
	{
		base = vec3(dot(base, sRGB));
	}
	else if ( greyscale != 0 )
	{
		vec3 luma = vec3(dot(base, sRGB));
		base = mix(base, luma, greyscale);
	}

	if ( gamma != 1.0 )
	{
		out_color = vec4(pow(base, vec3(gamma)) * obScale, 1);
	}
	else
	{
		out_color = vec4(base * obScale, 1);
	}

	out_color.rgb = dither(out_color.rgb, frag_tex_coord);
}
