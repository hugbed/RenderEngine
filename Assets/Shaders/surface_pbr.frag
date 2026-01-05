#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

// --- Constants --- //

layout(push_constant)
    uniform MaterialIndex {
	    layout(offset = 0) uint sceneNodeIndex;
	    layout(offset = 4) uint materialIndex; // index into material.properties
    } pc;

#include "view.glsl"

#include "pbr.glsl"

layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint transforms;
  uint lights;
  uint lightCount;
  uint materials;
  uint shadows;
  uint pad0; uint pad1;
} uDrawParams;

#define GetView() GetResource(ViewUniforms, uDrawParams.view).view

void main() {
    View view = GetView();
    float exposure = view.exposure;
    uint debugInput = view.debugInput;
    uint debugEquation = view.debugEquation;
    vec4 color = BRDF_Lighting(
        fragPos, fragTexCoord, fragNormal, viewPos,
        uDrawParams.materials, pc.materialIndex,
        uDrawParams.lights, uDrawParams.lightCount,
        uDrawParams.shadows, 
        view);
    outColor = color;
}
