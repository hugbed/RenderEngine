#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "material_common.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

#include "phong.glsl"

layout(constant_id = CONSTANT_NB_LIGHTS)
    const uint NB_LIGHTS = 1;

layout(set = SET_VIEW, binding = VIEW_BINDINGS_LIGHTS)
    uniform Lights {
        Light light[NB_LIGHTS];
    } lights;

// --- Set 2 (Material Uniforms) --- //

struct EnvironmentProperties {
    float ior;
    float metallic; // reflection {0, 1}
    float transmission; // refraction [0..1]
};

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_PROPERTIES)
    uniform MaterialProperties {
        PhongMaterial phong;
        EnvironmentProperties env;
    } material;

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_TEX)
    uniform sampler2D texSamplers[PHONG_TEX_COUNT];

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_TEX_ENV)
    uniform samplerCube environmentSampler;

#include "shadow.glsl"

void main() {
    vec3 normal = normalize(fragNormal);

    // --- Shading --- //

    // todo: have #define to choose either rgba or texture instead of using both
    vec4 diffuse = material.phong.diffuse * texture(texSamplers[PHONG_TEX_DIFFUSE], fragTexCoord);
    vec4 specular = material.phong.specular * texture(texSamplers[PHONG_TEX_SPECULAR], fragTexCoord);
    PhongMaterial phongMaterial = PhongMaterial(diffuse, specular, material.phong.shininess);
    
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < NB_LIGHTS; ++i)
    {
        float shadow = ComputeShadow(lights.light[i], fragPos, normal);
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
