#version 450

// DrawBlurPic: rectangle in NDC + mask texture coordinates via push constants

layout(push_constant) uniform PushConstants {
	vec4 rect;	// x0, y0, x1, y1 in NDC
	vec4 uv;	// s1, t1, s2, t2 of mask
	vec4 params;	// x = alpha
} pc;

layout(location = 0) out vec2 mask_uv;
layout(location = 1) out vec2 screen_uv;

out gl_PerVertex {
	vec4 gl_Position;
};

void main() {
	vec2 f = vec2( float( gl_VertexIndex & 1 ), float( ( gl_VertexIndex >> 1 ) & 1 ) );
	vec2 p = mix( pc.rect.xy, pc.rect.zw, f );
	gl_Position = vec4( p, 0.0, 1.0 );
	mask_uv = mix( pc.uv.xy, pc.uv.zw, f );
	screen_uv = p * 0.5 + 0.5;
}
