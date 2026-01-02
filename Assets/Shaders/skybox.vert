#version 450

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragTexCoords;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Descriptors --- //

RegisterUniform(ViewUniforms, {
    mat4 view;
    mat4 proj;
    vec3 pos;
    // float exposure;
});

layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint skyboxTexture;
  uint pad0; uint pad1;
} uDrawParams;

#define GetView() GetResource(ViewUniforms, uDrawParams.view)

// ---

void main()
{
    fragTexCoords = inPosition;
    vec4 pos = GetView().proj * mat4(mat3(GetView().view)) * vec4(inPosition, 1.0);
    gl_Position = pos.xyww; // project at infinity (w = 1)
}
