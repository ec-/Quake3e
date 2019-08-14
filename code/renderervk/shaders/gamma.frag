#version 450

layout(push_constant) uniform params {
	vec4 gammaOverbright;
};

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput in_color;

layout(location = 0) out vec4 out_color;

void main() {
	vec3 base = subpassLoad(in_color).rgb;

	out_color.rgb = pow(base, gammaOverbright.xyz) * gammaOverbright.w;
}
