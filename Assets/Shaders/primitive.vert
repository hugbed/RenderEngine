#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec3 viewPos;

// --- Push Contants -- //

layout(push_constant)
    uniform SceneNodeIndex {
	    layout(offset = 0) uint sceneNodeIndex; // index into MeshTransforms.transforms
        layout(offset = 4) uint materialIndex;
    } pc;

// --- Descriptors --- //

RegisterUniform(ViewUniforms, {
    mat4 view;
    mat4 proj;
    vec3 pos;
    // float exposure;
});

RegisterBuffer(std430, readonly, MeshTransforms, {
    mat4 transforms[];
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

#define GetView() GetResource(ViewUniforms, uDrawParams.view)
#define GetTransforms() GetResource(MeshTransforms, uDrawParams.transforms).transforms

// ---

void main() {
    mat4 transform = GetTransforms()[pc.sceneNodeIndex];
    fragPos = vec3(transform * vec4(inPosition, 1.0));
    gl_Position = GetView().proj * GetView().view * vec4(fragPos, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transform) * inNormal; // assumes afine transform
    viewPos = GetView().pos;
}
