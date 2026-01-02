#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Descriptors --- //

// For unlit, there's only a single material
RegisterUniform(MaterialProperties, {
    vec4 baseColor;
    vec4 secondaryColor;
    uint texture;
    uint pad0; uint pad1; uint pad2;
});

// Draw parameters
layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint transforms;
  uint lights;
  uint lightCount;
  uint materials;
  uint shadowTransforms;
  uint pad0; uint pad1;
} uDrawParams;

#define GetMaterial() GetResource(MaterialProperties, uDrawParams.materials)

void main() {
    outColor = GetMaterial().baseColor * texture(uGlobalTextures2D[GetMaterial().texture], fragTexCoord);
}
