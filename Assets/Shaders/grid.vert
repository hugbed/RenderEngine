
#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / outputs --- //

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 nearPoint;
layout(location = 2) out vec3 farPoint;
layout(location = 3) out mat4 fragView;
layout(location = 7) out mat4 fragProj;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Descriptors --- //

#include "view.glsl"

layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint pad0; uint pad1; uint pad3;
} uDrawParams;

#define GetView() GetResource(ViewUniforms, uDrawParams.view).view

// ---

float gridSize = 1;

vec3 gridPlane[6] = vec3[](
    vec3(gridSize, gridSize, 0),
    vec3(-gridSize, -gridSize, 0),
    vec3(-gridSize, gridSize, 0),
    vec3(-gridSize, -gridSize, 0),
    vec3(gridSize, gridSize, 0),
    vec3(gridSize, -gridSize, 0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 view, mat4 projection)
{
    mat4 viewInv = inverse(view);
    mat4 projInverse = inverse(projection);
    vec4 unprojectedPoint = viewInv * (projInverse * vec4(x, y, z, 1.0));
    return (unprojectedPoint.xyz / unprojectedPoint.w);
}

void main() {
    vec3 p0 = gridPlane[gl_VertexIndex].xyz;
    nearPoint = UnprojectPoint(p0.x, p0.y, 0.0, GetView().view, GetView().proj).xyz;
    farPoint = UnprojectPoint(p0.x, p0.y, 1.0, GetView().view, GetView().proj).xyz;

    fragPos = p0;
    fragView = GetView().view;
    fragProj = GetView().proj;
    gl_Position = vec4(p0, 1.0);
}
