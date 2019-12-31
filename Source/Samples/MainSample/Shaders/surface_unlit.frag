#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewDir;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

// --- Set 1 (Model Uniforms) --- //
// ...

// --- Set 2 (Material Uniforms) --- //

layout(set = 2, binding = 0) uniform MaterialProperties {
    vec4 baseColor;
    // To use the same layout as the lit shader
    vec4 secondaryColor; float padding;
} material;

// Texture for base color
layout(set = 2, binding = 1) uniform sampler2D texSamplers[1];

void main() {
    outColor = material.baseColor * texture(texSamplers[0], fragTexCoord);
}
