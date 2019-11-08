#version 450

// 128 bytes
layout(push_constant) uniform Transform {
	mat4 clip_space_xform;
	mat3x4 eye_space_xform;
	vec4 clipping_plane; // in eye space
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
layout(location = 1) in vec4 in_color;
//layout(location = 2) in vec2 in_tex_coord0;
//layout(location = 3) in vec2 in_tex_coord1;
layout(location = 4) in vec3 in_normal;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_tex_coord0;
//layout(location = 2) out vec2 frag_tex_coord1;
layout(location = 3) out vec2 fog_tex_coord;

layout (constant_id = 0) const int clip_plane = 0;

// out gl_PerVertex {
//	vec4 gl_Position;
//	float gl_ClipDistance[1];
// };

void main() {
	vec4 p = vec4(in_position, 1.0);
	gl_Position = clip_space_xform * p;

	if ( clip_plane != 0 ) {
		gl_ClipDistance[0] = dot(clipping_plane, vec4(p * eye_space_xform, 1.0));
	} else {
		gl_ClipDistance[0] = 0.0;
	}

	frag_color = in_color;

	//frag_tex_coord0 = in_tex_coord0;
	// environment mapping
	vec3 viewer = normalize(eyePos.xyz - in_position);
	float d = dot(in_normal, viewer); 
	vec2 reflected = in_normal.yz * 2 * d - viewer.yz;

	frag_tex_coord0.s = 0.5 + reflected.x * 0.5;
	frag_tex_coord0.t = 0.5 - reflected.y * 0.5;

	// fog calculations...

	float s = dot(in_position, fogDistanceVector.xyz) + fogDistanceVector.w;
	float t = dot(in_position, fogDepthVector.xyz) + fogDepthVector.w;

	if ( fogEyeT.y == 1.0 ) {
		if ( t < 0.0 ) {
			t = 1.0 / 32.0;
		} else {
			t = 31.0 / 32.0;
		}
	} else {
		if ( t < 1.0 ) {
			t = 1.0 / 32.0;
		} else {
			t = 1.0 / 32.0 + (30.0 / 32.0 * t) / ( t - fogEyeT.x );
		}
	}

	fog_tex_coord = vec2(s, t);
}
