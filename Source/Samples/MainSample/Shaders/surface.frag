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

layout(constant_id = CONSTANT_NB_MATERIAL_PROPERTIES)
    const uint NB_MATERIAL_PROPERTIES = 64;

layout(push_constant)
    uniform MaterialIndex {
	    layout(offset = 4) uint materialIndex; // index into material.properties
    } pc;

layout(set = SET_VIEW, binding = BINDING_VIEW_LIGHTS)
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

struct MaterialProperties {
    PhongProperties phong;
    EnvironmentProperties env;
    PhongTextures phongTextures;
};

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_PROPERTIES)
    uniform MaterialPropertiesUBO {
        MaterialProperties properties[NB_MATERIAL_PROPERTIES];
    } material;

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_SAMPLERS_2D)
    uniform sampler2D textures_2d[NB_MATERIAL_SAMPLERS_2D];

layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_SAMPLERS_CUBE)
    uniform samplerCube textures_cube[NB_MATERIAL_SAMPLERS_CUBE];

#include "shadow.glsl"

void main() {
    vec3 normal = normalize(fragNormal);

    // --- Shading --- //

    MaterialProperties props = material.properties[pc.materialIndex];

    // todo: have #define to choose either rgba or texture instead of using both
    vec4 diffuse = props.phong.diffuse * texture(textures_2d[props.phongTextures.diffuse], fragTexCoord);
    vec4 specular = props.phong.specular * texture(textures_2d[props.phongTextures.specular], fragTexCoord);
    // vec4 shininess = props.phong.shininess * texture(textures_2d[props.phongTextures.shininess], fragTexCoord);
    PhongProperties phongProperties = PhongProperties(diffuse, specular, props.phong.shininess);
    
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
    if (props.env.metallic > 0.0) // todo: have 
    {
        // float metallic = min(0.0, props.env.metallic);
        dir = -reflect(viewDir, normal);
        shadedColor = mix(shadedColor, texture(textures_cube[props.env.cubeMapTexture], dir).rgb, vec3(props.env.metallic));
    }

    // refraction
    if (props.env.transmission > 0.0)
    {
        // float transmission = min(0.0, props.env.transmission);
        dir = refract(viewDir, normal, 1.0 / props.env.ior);
        shadedColor = mix(shadedColor, texture(textures_cube[props.env.cubeMapTexture], dir).rgb, vec3(props.env.transmission));
    }

    // output + transparency
    outColor = vec4(shadedColor, diffuse.a);
}
