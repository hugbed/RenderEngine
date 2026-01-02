#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec3 viewPos;

layout(location = 0) out vec4 outColor;

#include "phong.glsl"

// --- Constants --- //

layout(push_constant)
    uniform MaterialIndex {
	    layout(offset = 0) uint unused0;
	    layout(offset = 4) uint materialIndex; // index into material.properties
    } pc;

// --- Descriptors --- //

// todo (hbdard): convert lights to a buffer
RegisterBuffer(std430, readonly, Lights, {
    Light light[];
});

struct EnvironmentProperties {
    float ior;
    float metallic; // reflection {0, 1}
    float transmission; // refraction [0..1]
    int cubeMapTexture; // TextureHandle
};

struct MaterialProperties {
    PhongProperties phong;
    EnvironmentProperties env;
    PhongTextures phongTextures;
};

RegisterBuffer(std430, readonly, MaterialPropertiesUBO, {
    MaterialProperties properties[];
});

layout(set = 1, binding = 0) uniform DrawParameters {
  uint view;
  uint transforms;
  uint lights;
  uint lightCount;
  uint materials;
  uint shadows;
  uint pad0; uint pad1;
} uDrawParams;

#define GetLights() GetResource(Lights, uDrawParams.lights).light

#include "shadow.glsl"

void main() {
    vec3 normal = normalize(fragNormal);

    // --- Shading --- //

    MaterialProperties props = GetResource(MaterialPropertiesUBO, uDrawParams.materials).properties[pc.materialIndex];

    // todo: have #define to choose either rgba or texture instead of using both
    vec4 diffuse = props.phong.diffuse * texture(GetTexture2D(props.phongTextures.diffuse), fragTexCoord);
    vec4 specular = props.phong.specular * texture(GetTexture2D(props.phongTextures.specular), fragTexCoord);
    // vec4 shininess = props.phong.shininess * texture(GetTexture2D()[props.phongTextures.shininess], fragTexCoord);
    PhongProperties phongProperties = PhongProperties(diffuse, specular, props.phong.shininess);
    
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < uDrawParams.lightCount; ++i)
    {
        float shadow = ComputeShadow(GetLights()[i], fragPos, normal);
        shadedColor += PhongLighting(GetLights()[i], phongProperties, normal, fragPos, viewPos, shadow).rgb;
    }

    // --- Environment mapping --- //

    vec3 viewDir = normalize(viewPos - fragPos);
    vec3 dir; // cubeMap sampling vector

    // reflection
    if (props.env.metallic > 0.0) // todo: have 
    {
        // float metallic = min(0.0, props.env.metallic);
        dir = -reflect(viewDir, normal);
        shadedColor = mix(shadedColor, texture(GetTextureCube(props.env.cubeMapTexture), dir).rgb, vec3(props.env.metallic));
    }

    // refraction
    if (props.env.transmission > 0.0)
    {
        // float transmission = min(0.0, props.env.transmission);
        dir = refract(viewDir, normal, 1.0 / props.env.ior);
        shadedColor = mix(shadedColor, texture(GetTextureCube(props.env.cubeMapTexture), dir).rgb, vec3(props.env.transmission));
    }

    // output + transparency
    outColor = vec4(shadedColor, diffuse.a);
}
