#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

// --- Constants --- //

// todo (hbedard): use draw indirect instead
layout(push_constant)
    uniform ShadowIndex {
	    layout(offset = 0) uint shadowIndex; // index into shadow.transforms
	    layout(offset = 4) uint sceneTreeNodeIndex; // index into model.transforms
    } pc;

// --- Descriptors --- //

struct ShadowView {
    mat4 view;
    mat4 proj;
    vec3 pos;
};

RegisterBuffer(std430, readonly, MeshTransforms, {
    mat4 transforms[];
});

RegisterBuffer(std430, readonly, ShadowViews, {
    ShadowView views[];
});

layout(set = 1, binding = 0) uniform DrawParameters {
    uint meshTransforms;
    uint shadowViews;
    uint pad0; uint pad1;
} uDrawParams;

#define GetMeshTransforms() GetResource(MeshTransforms, uDrawParams.meshTransforms).transforms
#define GetShadowViews() GetResource(ShadowViews, uDrawParams.shadowViews).views

// ---

void main() {
    vec3 fragPos = vec3(GetMeshTransforms()[pc.sceneTreeNodeIndex] * vec4(inPosition, 1.0));
    ShadowView shadow = GetShadowViews()[pc.shadowIndex];
    gl_Position = shadow.proj * shadow.view * vec4(fragPos, 1.0);
}
