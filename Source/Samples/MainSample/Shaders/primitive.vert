#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec3 viewPos;

//--- Set 0 (Scene Uniforms) --- //
#include "view_set.glsl"

// --- Set 1 (Model Uniforms) --- //
layout(set = SET_MODEL, binding = BINDING_MODEL_UNIFORMS) uniform ModelUniforms {
    mat4 transform;
} model;

void main() {
    fragPos = vec3(model.transform * vec4(inPosition, 1.0));
    gl_Position = view.proj * view.view * vec4(fragPos, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = mat3(model.transform) * inNormal; // assumes afine transform
    viewPos = view.pos;
}
