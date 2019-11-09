#version 450

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
layout(set = 1, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 1) in vec3 N;  // normalized object-space normal vector
layout(location = 2) in vec4 L;  // object-space light vector
layout(location = 3) in vec4 V;  // object-space view vector
//layout(location = 4) in vec2 fog_tex_coord;

layout (constant_id = 0) const int alpha_test_func = 0;
layout (constant_id = 1) const float alpha_test_value = 0.0;

layout(location = 0) out vec4 out_color;

void main() {
	vec4 base = texture(texture0, frag_tex_coord);

	// specialization: alpha-test function
	if (alpha_test_func == 1) {
		if (base.a == alpha_test_value) discard;
	} else if (alpha_test_func == 2) {
		if (base.a >= alpha_test_value) discard;
	} else if (alpha_test_func == 3) {
		if (base.a < alpha_test_value) discard;
	}

	vec4 lightColorRadius = lightColor;

	// project fragment on light vector
	float scale = clamp( dot( -L.xyz, lightVector.xyz ) * lightVector.w, 0.0, 1.0 );
	//
	vec4 LL = lightVector * scale + L;

	vec3 nL = normalize(LL.xyz); // normalized light vector
	vec3 nV = normalize(V.xyz);	// normalized view vector

	// light intensity
	float intensFactor = 1.0 - (dot(LL.xyz, LL.xyz) * lightColorRadius.w);
	if (intensFactor <= 0.0)
		discard;
	vec3 intens = lightColorRadius.rgb * intensFactor;

	// Lambertian diffuse reflection term (N.L)
	float diffuse = dot(N, nL);

	// specular reflection term (N.H)
	float specFactor = dot(N, normalize(nL + nV));

	//float spec = pow(specFactor, 8.0) * 0.25;
	vec4 spec = vec4(pow(specFactor, 10.0)*0.25) * base * 0.8;

	out_color = (base * vec4(diffuse) + spec) * vec4(intens, 1.0);
}
