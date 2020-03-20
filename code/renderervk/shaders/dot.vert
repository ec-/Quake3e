#version 450

// 128 bytes
layout(push_constant) uniform Transform {
	mat4 mvp;
};

layout(location = 0) in vec3 in_position;

out gl_PerVertex {
	vec4 gl_Position;
	float gl_PointSize;
};

void main() {
	gl_Position = mvp * vec4(in_position, 1.0);
	gl_PointSize = 1.0;
}
