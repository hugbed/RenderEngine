// assumes: #include "bindless.glsl"

#define PI 3.14159265359

struct Material
{
    vec4 baseColor; // linear RGB [0..1]
    vec4 emissive; // linear RGB [0..1] + exposure compensation
    float reflectance; // [0..1]
    float metallic; // [0..1]
    float perceptualRoughness; // [0..1]
    float ambientOcclusion; // [0..1]
    uint baseColorTexture;
    uint emissiveTexture;
    uint metallicTexture;
    uint roughnessTexture;
    uint normalsTexture;
    uint ambientOcclusionTexture;
    uint pad0;
    uint pad1;
};
RegisterBuffer(std430, readonly, MaterialBuffer, {
    Material materials[];
});
#define GetMaterials(bufferHandle) GetResource(MaterialBuffer, bufferHandle).materials

const uint LIGHT_TYPE_DIRECTIONAL = 1;
const uint LIGHT_TYPE_POINT = 2;
const uint LIGHT_TYPE_SPOT = 3;

struct Light
{
    vec3 color;
    vec3 position;
    vec3 direction; // directional/spot
    float intensity; // illuminance in lx (directional) or luminous power in lm
    float falloffRadius; // point/spot
    float cosInnerAngle; // spot (cos of the innerAngle)
    float cosOuterAngle; // spot (cos of the outerAngle)
    uint shadowIndex;
    uint type;
};
RegisterBuffer(std430, readonly, LightBuffer, {
    Light lights[];
});
#define GetLights(bufferHandle) GetResource(LightBuffer, bufferHandle).lights

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

vec3 F_Schlick(float u, vec3 f0)
{
    return f0 + (vec3(1.0) - f0) * pow(1.0 - u, 5.0);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert()
{
    return 1.0 / PI;
}

vec3 RemapBaseColor(Material pbr)
{
    return (1.0 - pbr.metallic) * pbr.baseColor.rgb;
}

vec3 RemapReflectance(Material pbr)
{
    return vec3(0.16 * pbr.reflectance * pbr.reflectance * (1.0 - pbr.metallic) + pbr.baseColor * pbr.metallic);
}

vec3 BRDF(vec3 n, vec3 v, vec3 l, Material pbr)
{
    vec3 diffuseColor = RemapBaseColor(pbr);
    vec3 f0 = RemapReflectance(pbr);

    vec3 h = normalize(v + l);

    float NoV = abs(dot(n, v)) + 1e-5;
    float NoL = clamp(dot(n, l), 0.0, 1.0);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float LoH = clamp(dot(l, h), 0.0, 1.0);

    // perceptually linear roughness to roughness (see parameterization)
    float roughness = pbr.perceptualRoughness * pbr.perceptualRoughness;

    float D = D_GGX(NoH, roughness);
    vec3  F = F_Schlick(LoH, f0);
    float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

    // specular BRDF
    vec3 Fr = (D * V) * F;

    // diffuse BRDF
    vec3 Fd = diffuseColor * Fd_Lambert();

    return Fr + Fd;
}

float ComputeSquareFalloff(vec3 l, float falloffRadius)
{
    float inverseFalloffRadius = 1.0 / max(falloffRadius, 0.083);
    float distanceSquare = dot(l, l);
    float factor = distanceSquare * inverseFalloffRadius * inverseFalloffRadius;
    float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}

float ComputeSpotAngleAttenuation(vec3 l, vec3 lightDir, float cosInnerAngle, float cosOuterAngle)
{
    // the scale and offset computations can be done CPU-side
    float spotScale = 1.0 / max(cosInnerAngle - cosOuterAngle, 1e-4);
    float spotOffset = -cosOuterAngle * spotScale;

    float cd = dot(normalize(-lightDir), l);
    float attenuation = clamp(cd * spotScale + spotOffset, 0.0, 1.0);
    return attenuation * attenuation;
}

vec3 EvaluatePunctualLight(Light light, vec3 brdf, vec3 l, float NoL)
{
    float attenuation = 1.0; // no attenuation for directional lights
    if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT)
    {
        attenuation = ComputeSquareFalloff(l, light.falloffRadius);

        if (light.type == LIGHT_TYPE_SPOT)
        {
            attenuation = attenuation * ComputeSpotAngleAttenuation(
                l,
                light.direction,
                light.cosInnerAngle,
                light.cosOuterAngle);
        }
    }
    return (brdf * light.intensity * attenuation * NoL) * light.color;
}

// Computes the camera's EV100 from exposure settings
// aperture in f-stops
// shutterSpeed in seconds
// sensitivity in ISO
float EV100FromExposureSettings(float aperture, float shutterSpeed, float sensitivity) {
    return log2((aperture * aperture) / shutterSpeed * 100.0 / sensitivity);
}

// Computes the exposure normalization factor from
// the camera's EV100
float exposureFromEV100(float ev100) {
    return 1.0 / (pow(2.0, ev100) * 1.2);
}

#include "shadow.glsl"

vec3 BRDF_Lighting(
    vec3 worldPosition, vec3 normal, vec3 viewPosition,
    uint materialBuffer, uint materialHandle,
    uint lightBuffer, uint lightCount,
    uint shadowBuffer) 
{
    Material pbr = GetMaterials(materialBuffer)[materialHandle];
    
    vec3 l0 = vec3(0.0);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        Light light = GetLights(lightBuffer)[lightIndex];
        float shadow = 0.0;
        if (light.type == LIGHT_TYPE_DIRECTIONAL)
        { 
            shadow = ComputeShadow(light, worldPosition, normal, shadowBuffer);
        }
        vec3 n = normalize(normal);
        vec3 v = normalize(viewPosition - worldPosition);
        vec3 l = normalize(light.position - worldPosition);
        float NoL = clamp(dot(n, l), 0.0, 1.0);
        l0 = l0 + (1.0 - shadow) * EvaluatePunctualLight(light, BRDF(n, v, l, pbr), l, NoL);
    }

    // todo (hbedard): use IBL for indirect lighting
    vec3 ambient = vec3(0.03) * pbr.baseColor.xyz * (1.0 - pbr.ambientOcclusion);
    float ev100 = EV100FromExposureSettings(2.8, 0.5, 800.0);
    float exposure = exposureFromEV100(ev100);
    vec3 color = ambient + l0 * exposure;
    return color;
}
