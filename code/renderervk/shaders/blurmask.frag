#version 450

// blurred frame, opacity = mask alpha * alpha

layout(set = 0, binding = 0) uniform sampler2D blur_tex;
layout(set = 1, binding = 0) uniform sampler2D mask_tex;

layout(push_constant) uniform PushConstants {
	vec4 rect;
	vec4 uv;
	vec4 params;
} pc;

layout(location = 0) in vec2 mask_uv;
layout(location = 1) in vec2 screen_uv;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4( texture( blur_tex, screen_uv ).rgb, texture( mask_tex, mask_uv ).a * pc.params.x );
}
