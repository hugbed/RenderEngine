#version 450

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Descriptors --- //

layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint skyboxTexture;
  uint pad0; uint pad1;
} uDrawParams;

// ---

void main()
{    
    outColor = texture(GetTextureCube(uDrawParams.skyboxTexture), fragTexCoord);
}
