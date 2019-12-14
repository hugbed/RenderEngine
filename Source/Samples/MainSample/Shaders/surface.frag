#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;

layout(constant_id = 0) const int LIGHTING_MODEL = 0;

// Set 0: always bound (nothing yet)

// Set 1: bound for each object (nothing yet)

// Set 2: bound for each material (nothing yet)
layout(set = 2, binding = 0) uniform MaterialProperties
{
	vec4 ambient;
	vec4 diffuse;
	vec4 specular;
	float opacity;
} material;

layout(set = 2, binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    switch (LIGHTING_MODEL)
    {
        case 0: // Unlit 
            outColor = vec4(fragColor, 1.0);
            break;
        case 1: // Textured
            outColor = material.diffuse * texture(texSampler, fragTexCoord);
            break;
    }
}
