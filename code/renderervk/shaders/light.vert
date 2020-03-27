#version 450

// 64 bytes
layout(push_constant) uniform Transform {
	mat4 mvp;
};

layout(set = 0, binding = 0) uniform UBO {
	// VERTEX
	vec4 eyePos;
	vec4 lightPos;
	//  VERTEX-FOG
	vec4 fogDistanceVector;
	vec4 fogDepthVector;
	vec4 fogEyeT;
	// FRAGMENT
	vec4 lightColor;
	vec4 fogColor;
	// linear dynamic light
	vec4 lightVector;
};

layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_tex_coord;
layout(location = 5) in vec3 in_normal;

layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec3 N; // normal array 
layout(location = 2) out vec4 L; // object-space light vector
layout(location = 3) out vec4 V; // object-space view vector
//layout(location = 4) out vec2 fog_tex_coord;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	gl_Position = mvp * vec4(in_position, 1.0);

	frag_tex_coord = in_tex_coord;
	N = in_normal;
	L = lightPos - vec4(in_position, 1.0);
	V = eyePos - vec4(in_position, 1.0);
}
