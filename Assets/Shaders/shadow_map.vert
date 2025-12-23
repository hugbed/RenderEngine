#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(push_constant)
    uniform ShadowIndex {
	    layout(offset = 0) uint shadowIndex; // index into shadow.transforms
	    layout(offset = 4) uint sceneTreeNodeIndex; // index into model.transforms
    } pc;

//--- Set 0 (Scene Uniforms) --- //

struct ShadowView {
    mat4 view;
    mat4 proj;
    vec3 pos;
};

layout(set = 0, binding = 0)
    readonly buffer ViewUniforms {
        ShadowView transforms[];
    } shadow;

// --- Set 1 (Model Uniforms) --- //

layout(set = 1, binding = 0)
    readonly buffer ModelUniforms {
        mat4 transforms[];
    } sceneTree;

void main() {
    vec3 fragPos = vec3(sceneTree.transforms[pc.sceneTreeNodeIndex] * vec4(inPosition, 1.0));
    ShadowView shadow = shadow.transforms[pc.shadowIndex];
    gl_Position = shadow.proj * shadow.view * vec4(fragPos, 1.0);
}
