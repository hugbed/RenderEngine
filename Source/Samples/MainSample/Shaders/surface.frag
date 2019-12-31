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

layout(set = 2, binding = 0) uniform MaterialProperties {
    PhongMaterial phong;
} material;

const uint PHONG_TEX_DIFFUSE = 0;
const uint PHONG_TEX_SPECULAR = 1;
const uint PHONG_TEX_COUNT = 2;

layout(set = 2, binding = 1) uniform sampler2D texSamplers[PHONG_TEX_COUNT];

void main() {
    vec4 shadedColor = vec4(0.0, 0.0, 0.0, 1.0);

    // todo: have #define to choose either vec4 or texture instead of using both
    vec4 diffuse = material.phong.diffuse * texture(texSamplers[PHONG_TEX_DIFFUSE], fragTexCoord);
    vec4 specular = material.phong.specular * texture(texSamplers[PHONG_TEX_SPECULAR], fragTexCoord);
    PhongMaterial phongMaterial = PhongMaterial(diffuse, specular, material.phong.shininess);
    
    for (int i = 0; i < NB_POINT_LIGHTS; ++i)
        shadedColor += PhongLighting(lights.light[i], phongMaterial, normalize(fragNormal), fragPos, viewPos);

    outColor = vec4(shadedColor.rgb, diffuse.a);
}
