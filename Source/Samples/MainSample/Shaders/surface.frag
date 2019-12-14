#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

//--- Set 0 (Scene Uniforms) --- //

#ifdef USE_LIGHTS

struct PointLight {
    vec3 pos;
    vec3 colorDiffuse;
    vec3 colorSpecular;
};

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
} material;

layout(set = 2, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
#ifdef USE_LIGHTS
    // todo: fill this
    outColor = material.diffuse * texture(texSampler, fragTexCoord);
#else
    outColor = material.diffuse * texture(texSampler, fragTexCoord);
#endif
}
