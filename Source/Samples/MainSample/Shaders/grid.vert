
#version 450
#extension GL_ARB_separate_shader_objects : enable

//--- Set 0 (Scene Uniforms) --- //
#include "view_set.glsl"

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 nearPoint;
layout(location = 2) out vec3 farPoint;
layout(location = 3) out mat4 fragView;
layout(location = 7) out mat4 fragProj;

float gridSize = 1;

vec3 gridPlane[6] = vec3[](
    vec3(gridSize, gridSize, 0),
    vec3(-gridSize, -gridSize, 0),
    vec3(-gridSize, gridSize, 0),
    vec3(-gridSize, -gridSize, 0),
    vec3(gridSize, gridSize, 0),
    vec3(gridSize, -gridSize, 0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 view, mat4 projection)
{
    mat4 viewInv = inverse(view);
    mat4 projInverse = inverse(projection);
    vec4 unprojectedPoint = viewInv * (projInverse * vec4(x, y, z, 1.0));
    return (unprojectedPoint.xyz / unprojectedPoint.w);
}

// Set 2: bound for each material 

void main() {
    vec3 p0 = gridPlane[gl_VertexIndex].xyz;
    nearPoint = UnprojectPoint(p0.x, p0.y, 0.0, view.view, view.proj).xyz;
    farPoint = UnprojectPoint(p0.x, p0.y, 1.0, view.view, view.proj).xyz;

    fragPos = p0;
    fragView = view.view;
    fragProj = view.proj;
    gl_Position = vec4(p0, 1.0);
}
