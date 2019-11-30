#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(constant_id = 0) const int LIGHTING_MODEL = 0;
layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    switch (LIGHTING_MODEL)
    {
        case 0: // Unlit 
            outColor = vec4(fragColor, 1.0);
            break;
        case 1: // Textured
            outColor = texture(texSampler, fragTexCoord);
            break;
    }
}
