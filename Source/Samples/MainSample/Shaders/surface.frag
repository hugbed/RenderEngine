#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 viewDir;

//--- Set 0 (Scene Uniforms) --- //

#ifdef USE_LIGHTS

#include "phong.glsl"

layout(constant_id = 0) const uint NB_POINT_LIGHTS = 1;

layout(set = 0, binding = 1) uniform Lights {
    PointLight point[NB_POINT_LIGHTS];
} lights;

#endif // USE_LIGHTS


// --- Set 1 (Model Uniforms) --- //
// ...


// --- Set 2 (Material Uniforms) --- //

layout(set = 2, binding = 0) uniform MaterialProperties {
	vec4 ambient;
	vec4 diffuse;
	vec4 specular;
	float opacity;
    float shininess;
} material;

layout(set = 2, binding = 1) uniform sampler2D texSampler; // could be array of textures


layout(location = 0) out vec4 outColor;

void main() {
#ifdef USE_LIGHTS
    vec3 shadedColor = material.ambient.xyz;

    for (int i = 0; i < NB_POINT_LIGHTS; ++i)
        shadedColor += PhongPointLight(lights.point[i], fragNormal, fragPos, -normalize(viewDir), material.shininess);

    shadedColor *= material.diffuse.xyz * fragColor * texture(texSampler, fragTexCoord).xyz;
    outColor = vec4(shadedColor, 1.0);
#else
    outColor = material.diffuse * texture(texSampler, fragTexCoord);
#endif
}
