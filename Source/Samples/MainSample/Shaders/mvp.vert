#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

// Set 0: always bound 
layout(set = 0, binding = 0) uniform GlobalUniforms {
    mat4 view;
    mat4 proj;
} global_ubo;

// Set 1: bound per material (nothing yet)

// Set 2: bound for each object
layout(set = 2, binding = 0) uniform PerObjectUniforms {
    mat4 model;
} obj_ubo;

void main() {
    gl_Position = global_ubo.proj * global_ubo.view * obj_ubo.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
