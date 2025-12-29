#version 450

// --- Inputs / Outputs --- //

layout(location = 0) out vec2 fragTexCoord;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
	    layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Descriptors --- //

layout(set = 1, binding = 0) uniform DrawParameters {
    vec2 center; // = vec2(-0.6, -0.6);
    vec2 size; // = vec2(0.35, 0.35);
    uint texture;
    uint pad0; uint pad1; uint pad2;
} uDrawParams;

void main() {
    vec2 pos[4];
    pos[1] = vec2(+1.0, +1.0);
    pos[0] = vec2(-1.0, +1.0);
    pos[2] = vec2(-1.0, -1.0);
    pos[3] = vec2(+1.0, -1.0);

    gl_Position = vec4(uDrawParams.center + uDrawParams.size * pos[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord  = pos[gl_VertexIndex].xy * 0.5 + vec2(0.5, 0.5);
}
