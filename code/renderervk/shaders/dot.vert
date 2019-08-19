#version 450

// 128 bytes
layout(push_constant) uniform Transform {
	mat4 clip_space_xform;
};

layout(location = 0) in vec3 in_position;

out gl_PerVertex {
	vec4 gl_Position;
	float gl_PointSize;
};

void main() {
	vec4 p = vec4(in_position, 1.0);
	gl_Position = clip_space_xform * p;
	gl_PointSize = 1.0;
}
