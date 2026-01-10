#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 fragPos;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint mvpIndex;
        layout(offset = 4) uint equirectangularMapTexture;
    } pc;

// --- Descriptors --- //

RegisterUniform(ViewTransforms, { mat4 MVP[6]; });

// Draw Parameters
layout(set = 1, binding = 0) uniform DrawParameters {
    uint mvpBuffer;
    uint pad0; uint pad1; uint pad3;
} uDrawParams;

#define GetMVP() GetResource(ViewTransforms, uDrawParams.mvpBuffer).MVP[pc.mvpIndex]

// ---

void main()
{
    mat4 MVP = GetMVP();
    fragPos = inPos;
    gl_Position = MVP * vec4(fragPos, 1.0);
}
