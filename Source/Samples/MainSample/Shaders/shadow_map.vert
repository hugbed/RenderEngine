#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

//--- Set 0 (Scene Uniforms) --- //
#include "view_set.glsl"

// --- Set 1 (Model Uniforms) --- //

layout(constant_id = 0)
    const uint NB_MODELS = 64;

layout(push_constant)
    uniform ModelIndex {
	    layout(offset = 0) uint modelIndex; // index into model.transforms
    } pc;

layout(set = 1, binding = 0)
    uniform ModelUniforms {
        mat4 transforms[NB_MODELS];
    } model;

void main() {
    vec3 fragPos = vec3(model.transforms[pc.modelIndex] * vec4(inPosition, 1.0));
    gl_Position = view.proj * view.view * vec4(fragPos, 1.0);
}
