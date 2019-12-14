#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;

// Set 0: always bound 
layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;
    mat4 proj;
} view;

// Set 1: bound per object
layout(set = 1, binding = 0) uniform ModelUniforms {
    mat4 transform;
} model;

// Set 2: bound for each material 

void main() {
    gl_Position = view.proj * view.view * model.transform * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    fragNormal = mat3(model.transform) * inNormal; // assumes afine transform
}
