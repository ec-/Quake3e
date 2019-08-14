#version 450

// 128 bytes
layout(push_constant) uniform Transform {
	mat4 clip_space_xform;
	mat3x4 eye_space_xform;
	vec4 clipping_plane; // in eye space
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_tex_coord0;
//layout(location = 3) in vec2 in_tex_coord1;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_tex_coord0;
//layout(location = 2) out vec2 frag_tex_coord1;

layout (constant_id = 0) const int clip_plane = 0;

//out gl_PerVertex {
//    vec4 gl_Position;
//    float gl_ClipDistance[1];
//};

void main() {
	vec4 p = vec4(in_position, 1.0);
	gl_Position = clip_space_xform * p;

	if ( clip_plane != 0 ) {
		gl_ClipDistance[0] = dot(clipping_plane, vec4(p * eye_space_xform, 1.0));
	} else {
		gl_ClipDistance[0] = 0.0;
	}

	frag_color = in_color;
	frag_tex_coord0 = in_tex_coord0;
}
