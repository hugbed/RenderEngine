#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "phong.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;
layout(location = 4) in vec4 fragLightPos;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

layout(constant_id = 0) const uint NB_LIGHTS = 1;

layout(set = 0, binding = 1) uniform Lights {
    Light light[NB_LIGHTS];
} lights;

layout(push_constant) uniform ShadowBlock {
  layout(offset = 0) int mapIndex;
} shadowConst;

layout(set = 0, binding = 2) uniform sampler2D shadowMaps[NB_LIGHTS];

// --- Set 1 (Model Uniforms) --- //
// ...

// --- Set 2 (Material Uniforms) --- //

struct EnvironmentProperties {
    float ior;
    float metallic; // reflection {0, 1}
    float transmission; // refraction [0..1]
};

layout(set = 2, binding = 0) uniform MaterialProperties {
    PhongMaterial phong;
    EnvironmentProperties env;
} material;

layout(set = 2, binding = 1) uniform sampler2D texSamplers[PHONG_TEX_COUNT];

layout(set = 2, binding = 2) uniform samplerCube environmentSampler;

layout(constant_id = 1) const uint USE_SHADOWS = 1;

#include "shadow.glsl"

// todo: have #define to choose either rgba or texture instead of using both
void main() {
    vec3 normal = normalize(fragNormal);

    // --- Shading --- //

    vec4 diffuse = material.phong.diffuse * texture(texSamplers[PHONG_TEX_DIFFUSE], fragTexCoord);
    vec4 specular = material.phong.specular * texture(texSamplers[PHONG_TEX_SPECULAR], fragTexCoord);
    PhongMaterial phongMaterial = PhongMaterial(diffuse, specular, material.phong.shininess);
    
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < NB_LIGHTS; ++i)
    {
        float shadow = ComputeShadow(lights.light[i], shadowMaps[shadowConst.mapIndex], fragLightPos, fragPos, normal);
        shadedColor += PhongLighting(lights.light[i], phongMaterial, normal, fragPos, viewPos, shadow).rgb;
    }

    // --- Environment mapping --- //

    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 dir; // cubeMap sampling vector

    // reflection
    if (material.env.metallic > 0.0)
    {
        dir = -reflect(viewDir, normal);
        shadedColor = mix(shadedColor, texture(environmentSampler, dir).rgb, vec3(material.env.metallic));
    }

    // refraction
    if (material.env.transmission > 0.0)
    {
        dir = refract(viewDir, normal, 1.0 / material.env.ior);
        shadedColor = mix(shadedColor, texture(environmentSampler, dir).rgb, vec3(material.env.transmission));
    }

    // output + transparency
    outColor = vec4(shadedColor, diffuse.a);
}
