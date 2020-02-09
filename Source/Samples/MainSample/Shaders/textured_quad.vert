#version 450

layout(location = 0) out vec2 fragTexCoord;

layout(push_constant) uniform QuadInfo {
    vec2 center; // = vec2(-0.6, -0.6);
    vec2 size; // = vec2(0.35, 0.35);
} quad;

void main() {
    vec2 pos[4];
    pos[1] = vec2(+1.0, +1.0);
    pos[0] = vec2(-1.0, +1.0);
    pos[2] = vec2(-1.0, -1.0);
    pos[3] = vec2(+1.0, -1.0);

    gl_Position = vec4(quad.center + quad.size * pos[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord  = pos[gl_VertexIndex].xy * 0.5 + vec2(0.5, 0.5);
}
