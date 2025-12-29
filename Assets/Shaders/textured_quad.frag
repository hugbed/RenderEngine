#version 450

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// --- Constants --- ///

layout(constant_id = 0) const uint kIsGrayscale = 0;

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

// ---

void main() {
    if (kIsGrayscale == 1) {
        float c = texture(uGlobalTextures2D[uDrawParams.texture], fragTexCoord).r;
        outColor = vec4(c, c, c, 1.0);
    } else {
        outColor = vec4(texture(uGlobalTextures2D[uDrawParams.texture], fragTexCoord).xyz, 1.0);
    }
}
