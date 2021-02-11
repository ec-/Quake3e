#version 450

// 64 bytes
layout(push_constant) uniform Transform {
	mat4 mvp;
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color0;
layout(location = 2) in vec2 in_tex_coord0;
layout(location = 3) in vec2 in_tex_coord1;
//layout(location = 4) in vec2 in_tex_coord2;
//layout(location = 5) in vec3 in_normal;
layout(location = 6) in vec4 in_color1;
//layout(location = 7) in vec4 in_color2;

layout(location = 0) out vec4 frag_color0;
layout(location = 1) out vec4 frag_color1;
//layout(location = 2) out vec4 frag_color2;

layout(location = 3) out vec2 frag_tex_coord0;
layout(location = 4) out vec2 frag_tex_coord1;
//layout(location = 5) out vec2 frag_tex_coord2;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = mvp * vec4(in_position, 1.0);

	frag_color0 = in_color0;
	frag_color1 = in_color1;
	// frag_color2 = in_color2;

	frag_tex_coord0 = in_tex_coord0;
	frag_tex_coord1 = in_tex_coord1;
	//frag_tex_coord2 = in_tex_coord2;
}
