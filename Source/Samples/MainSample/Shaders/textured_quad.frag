#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const uint kIsGrayscale = 0;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    if (kIsGrayscale == 1) {
        float c = texture(texSampler, fragTexCoord).r;
        outColor = vec4(c, c, c, 1.0);
    } else {
        outColor = vec4(texture(texSampler, fragTexCoord).xyz, 1.0);
    }
}
