#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec3 viewPos;

//--- Set 0 (Scene Uniforms) --- //
#include "view_set.glsl"

// --- Set 1 (Scene Uniforms) --- //

layout(push_constant)
    uniform SceneNodeIndex {
	    layout(offset = 0) uint sceneNodeIndex; // index into sceneTree.transforms
    } pc;

layout(set = SET_SCENE, binding = BINDING_SCENE_UNIFORMS)
    readonly buffer ModelUniforms {
        mat4 transforms[];
    } scene;

void main() {
    mat4 transform = scene.transforms[pc.sceneNodeIndex];
    fragPos = vec3(transform * vec4(inPosition, 1.0));
    gl_Position = view.proj * view.view * vec4(fragPos, 1.0);
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transform) * inNormal; // assumes afine transform
    viewPos = view.pos;
}
