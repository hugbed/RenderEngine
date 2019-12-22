#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "phong.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 viewDir;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

#ifdef LIT

layout(constant_id = 0) const uint NB_POINT_LIGHTS = 1;

layout(set = 0, binding = 1) uniform Lights {
    PointLight point[NB_POINT_LIGHTS];
} lights;

#endif

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
    vec3 shadedColor = vec3(0.0, 0.0, 0.0);

#ifdef LIT
    PhongMaterial phongMaterial = {
        material.phong.diffuse * texture(texSamplers[PHONG_TEX_DIFFUSE], fragTexCoord).xyz,
        material.phong.specular * texture(texSamplers[PHONG_TEX_SPECULAR], fragTexCoord).xyz,
        material.phong.shininess
    };
    for (int i = 0; i < NB_POINT_LIGHTS; ++i)
        shadedColor += PhongPointLight(lights.point[i], phongMaterial, normalize(fragNormal), fragPos, -normalize(viewDir));
#endif

    outColor = vec4(shadedColor, 1.0);
}
