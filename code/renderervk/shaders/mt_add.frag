#version 450

layout(set = 1, binding = 0) uniform sampler2D texture0;
layout(set = 2, binding = 0) uniform sampler2D texture1;

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord0;
layout(location = 2) in vec2 frag_tex_coord1;

layout(location = 0) out vec4 out_color;

layout (constant_id = 0) const int alpha_test_func = 0;

void main() {
    vec4 color_a = frag_color * texture(texture0, frag_tex_coord0);
    vec4 color_b = texture(texture1, frag_tex_coord1);
    out_color = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);

    if (alpha_test_func == 1) {
        if (out_color.a == 0.0f) discard;
    } else if (alpha_test_func == 2) {
        if (out_color.a >= 0.5f) discard;
    } else if (alpha_test_func == 3) {
        if (out_color.a < 0.5f) discard;
    }
}
