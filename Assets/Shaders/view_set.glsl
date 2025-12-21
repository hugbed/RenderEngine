
#include "material_common.glsl"

// Shared set between most vertex shaders
layout(set = SET_VIEW, binding = BINDING_VIEW_UNIFORMS) uniform ViewUniforms {
    mat4 view;
    mat4 proj;
    vec3 pos;
} view;
