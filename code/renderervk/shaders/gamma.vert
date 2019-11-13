#version 450

const vec2 v[6] = vec2[6](
	vec2(-1.0f,-1.0f),
	vec2( 1.0f,-1.0f),
	vec2( 1.0f, 1.0f),

	vec2( 1.0f, 1.0f),
	vec2(-1.0f, 1.0f),
	vec2(-1.0f,-1.0f)
);

void main() {
	gl_Position = vec4( v[ gl_VertexIndex ], 0.0f, 1.0f );
}

