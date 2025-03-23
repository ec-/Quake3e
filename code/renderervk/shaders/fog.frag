#version 450

layout(set = 0, binding = 0) uniform UBO {
	// light/env parameters:
	vec4 eyePos;				// vertex
	vec4 lightPos;				// vertex: light origin
	vec4 lightColor;			// fragment: rgb + 1/(r*r)
	vec4 lightVector;			// fragment: linear dynamic light
//#ifdef USE_FOG
	// fog parameters:
	vec4 fogDistanceVector;		// vertex
	vec4 fogDepthVector;		// vertex
	vec4 fogEyeT;				// vertex
	vec4 fogColor;				// fragment
//#endif
};

layout(set = 2, binding = 0) uniform sampler2D fog_texture;

//layout(location = 0) in vec4 frag_color;
//layout(location = 1) in vec2 frag_tex_coord0;
//layout(location = 2) in vec2 frag_tex_coord1;
//layout(location = 3) in vec2 frag_tex_coord2;
layout(location = 4) in vec2 fog_tex_coord;

layout(location = 0) out vec4 out_color;

//layout(constant_id = 0) const int alpha_test_func = 0;

void main() {
    //vec4 base = frag_color * texture(texture0, frag_tex_coord0);
	//vec4 fog = texture(fog_texture, fog_tex_coord);

    //if (alpha_test_func == 1) {
    //    if (base.a == 0.0f) discard;
    //} else if (alpha_test_func == 2) {
    //    if (base.a >= 0.5f) discard;
    //} else if (alpha_test_func == 3) {
    //    if (base.a < 0.5f) discard;
    //}

	//fog = fog * fogColor;

	//out_color = mix( base, fog, fog.a );

	vec4 fog = texture(fog_texture, fog_tex_coord);
//	fog.a = 1.0;
	out_color = fog * fogColor;
}
