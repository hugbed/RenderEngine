#version 450

#include "bindless.glsl"

// --- Inputs / Outputs --- //

layout(location = 0) in vec3 fragPos;
layout(location = 0) out vec4 outColor;

// --- Push Constants --- //

layout(push_constant)
    uniform PushConstants {
        layout(offset = 0) uint mvpIndex;
        layout(offset = 4) uint equirectangularMapTexture;
    } pc;

// --- Descriptors --- //

RegisterUniform(ViewTransforms, { mat4 MVP[6]; });

// Draw Parameters
layout(set = 1, binding = 0) uniform DrawParameters {
    uint mvpBuffer;
    uint pad0; uint pad1; uint pad3;
} uDrawParams;

// ---

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv = SampleSphericalMap(normalize(fragPos));
    vec3 color = texture(GetTexture2D(pc.equirectangularMapTexture), uv).rgb;
    outColor = vec4(color.rgb, 1.0);
}
