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

layout(constant_id = CONSTANT_NB_MATERIAL_SAMPLERS_2D)
    const uint NB_MATERIAL_SAMPLERS_2D = 64;

layout(constant_id = CONSTANT_NB_MATERIAL_SAMPLERS_CUBE)
    const uint NB_MATERIAL_SAMPLERS_CUBE = 64;

layout(set = SET_VIEW, binding = VIEW_BINDINGS_LIGHTS)
    uniform Lights {
        Light light[NB_LIGHTS];
    } lights;

// --- Set 2 (Material Uniforms) --- //

struct EnvironmentProperties {
    float ior;
    float metallic; // reflection {0, 1}
    float transmission; // refraction [0..1]
    int cubeMapTexture; // index into textures_cube
};

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_PROPERTIES)
    uniform MaterialProperties {
        PhongProperties phong;
        PhongTextures phongTextures;
        EnvironmentProperties env;
    } material;

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_SAMPLERS_2D)
    uniform sampler2D textures_2d[NB_MATERIAL_SAMPLERS_2D];

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_SAMPLERS_CUBE)
    uniform samplerCube textures_cube[NB_MATERIAL_SAMPLERS_CUBE];

#include "shadow.glsl"

void main() {
    vec3 normal = normalize(fragNormal);

    // --- Shading --- //

    // todo: have #define to choose either rgba or texture instead of using both
    vec4 diffuse = material.phong.diffuse * texture(textures_2d[material.phongTextures.diffuse], fragTexCoord);
    vec4 specular = material.phong.specular * texture(textures_2d[material.phongTextures.specular], fragTexCoord);
    // vec4 shininess = material.phong.shininess * texture(textures_2d[material.phongTextures.shininess], fragTexCoord);
    PhongProperties phongProperties = PhongProperties(diffuse, specular, material.phong.shininess);
    
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < NB_LIGHTS; ++i)
    {
        float shadow = ComputeShadow(lights.light[i], fragPos, normal);
        shadedColor += PhongLighting(lights.light[i], phongProperties, normal, fragPos, viewPos, shadow).rgb;
    }

    // --- Environment mapping --- //

    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 dir; // cubeMap sampling vector

    // reflection
    if (material.env.metallic > 0.0) // todo: have 
    {
        // float metallic = min(0.0, material.env.metallic);
        dir = -reflect(viewDir, normal);
        shadedColor = mix(shadedColor, texture(textures_cube[material.env.cubeMapTexture], dir).rgb, vec3(material.env.metallic));
    }

    // refraction
    if (material.env.transmission > 0.0)
    {
        // float transmission = min(0.0, material.env.transmission);
        dir = refract(viewDir, normal, 1.0 / material.env.ior);
        shadedColor = mix(shadedColor, texture(textures_cube[material.env.cubeMapTexture], dir).rgb, vec3(material.env.transmission));
    }

    // output + transparency
    outColor = vec4(shadedColor, diffuse.a);
}
