#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(push_constant)
    uniform ShadowIndex {
	    layout(offset = 0) uint shadowIndex; // index into shadow.transforms
	    layout(offset = 4) uint modelIndex; // index into model.transforms
    } pc;

//--- Set 0 (Scene Uniforms) --- //

layout(constant_id = 0)
    const uint NB_SHADOW_CASTING_LIGHTS = 4;

struct ShadowView {
    mat4 view;
    mat4 proj;
    vec3 pos;
};

layout(set = 0, binding = 0)
    uniform ViewUniforms {
        ShadowView transforms[NB_SHADOW_CASTING_LIGHTS];
    } shadow;

// --- Set 1 (Model Uniforms) --- //

layout(constant_id = 1)
    const uint NB_MODELS = 64;

layout(set = 1, binding = 0)
    uniform ModelUniforms {
        mat4 transforms[NB_MODELS];
    } model;

void main() {
    vec3 fragPos = vec3(model.transforms[pc.modelIndex] * vec4(inPosition, 1.0));
    ShadowView shadow = shadow.transforms[pc.shadowIndex];
    gl_Position = shadow.proj * shadow.view * vec4(fragPos, 1.0);
}
