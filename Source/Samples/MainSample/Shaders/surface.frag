#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "phong.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

layout(constant_id = 0) const uint NB_POINT_LIGHTS = 1;

layout(set = 0, binding = 1) uniform Lights {
    Light light[NB_POINT_LIGHTS];
} lights;

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

// todo: have #define to choose either rgba or texture instead of using both
void main() {
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);

    // Phong
    vec4 diffuse = material.phong.diffuse * texture(texSamplers[PHONG_TEX_DIFFUSE], fragTexCoord);
    vec4 specular = material.phong.specular * texture(texSamplers[PHONG_TEX_SPECULAR], fragTexCoord);
    PhongMaterial phongMaterial = PhongMaterial(diffuse, specular, material.phong.shininess);
    
    for (int i = 0; i < NB_POINT_LIGHTS; ++i)
        shadedColor += PhongLighting(lights.light[i], phongMaterial, normalize(fragNormal), fragPos, viewPos).rgb;

    // Environment mapping
    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 n = normalize(fragNormal);
    vec3 dir; // cubeMap sampling vector

    // reflection (todo: allow metallic grayscale texture)
    if (material.env.metallic > 0.0)
    {
        dir = -reflect(viewDir, n);
        shadedColor = mix(shadedColor, texture(environmentSampler, dir).rgb, vec3(material.env.metallic));
    }

    if (material.env.transmission > 0.0)
    {
        dir = refract(viewDir, n, 1.0 / material.env.ior);
        shadedColor = mix(shadedColor, texture(environmentSampler, dir).rgb, vec3(material.env.transmission));
    }

    outColor = vec4(shadedColor, diffuse.a);
}
