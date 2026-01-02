#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "bindless.glsl"

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint unused0;
        layout(offset = 4) uint unused1;
    } pc;

// --- Draw Parameters --- //

layout(set = 1, binding = 0) uniform DrawParameters {
    uint meshTransforms;
    uint shadowViews;
    uint pad0; uint pad1;
} uDrawParams;

void main() {
    // gl_FragDepth is all we need
} 