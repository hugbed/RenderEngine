#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //
// ...

// --- Set 1 (Model Uniforms) --- //
// ...

// --- Set 2 (Material Uniforms) --- //

layout(set = 2, binding = 0) uniform MaterialProperties {
    float ior;
    float reflectRatio; // ratio between 0 and 1
} material;

layout(set = 2, binding = 1) uniform samplerCube texSampler;

#define REFRACT

void main()
{
    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 n = normalize(fragNormal);

    // reflection
    vec3 dir = -reflect(viewDir, n);
    vec3 reflectColor = texture(texSampler, dir).rgb;

    // refraction
    dir = refract(viewDir, n, 1.0 / material.ior);
    vec3 refractColor = texture(texSampler, dir).rgb;

    float k = material.reflectRatio;
    outColor = vec4(k * reflectColor + (1 - k) * refractColor, 1.0);
}
