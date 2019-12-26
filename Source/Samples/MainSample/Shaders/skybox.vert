#version 450
layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTexCoords;

//--- Set 0 (Scene Uniforms) --- //
layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 view;
    mat4 proj;
} view;

void main()
{
    fragTexCoords = inPosition;
    vec4 pos = view.proj * mat4(mat3(view.view)) * vec4(inPosition, 1.0);
    gl_Position = pos.xyww; // project at infinity (w = 1)
}
