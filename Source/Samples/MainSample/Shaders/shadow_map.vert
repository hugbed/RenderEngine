#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

//--- Set 0 (Scene Uniforms) --- //
#include "view_set.glsl"

// --- Set 1 (Model Uniforms) --- //
layout(set = 1, binding = 0) uniform ModelUniforms {
    mat4 transform;
} model;

void main() {
    vec3 fragPos = vec3(model.transform * vec4(inPosition, 1.0));
    gl_Position = view.proj * view.view * vec4(fragPos, 1.0);
}
