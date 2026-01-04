// assumes: #include "bindless.glsl"

#define PI 3.14159265359

// --- SRGB --- //

// sRGB conversion reference from [Moving Frostbite to PBR]
// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf

float approximationSRgbToLinear (float sRGBCol)
{
    return pow ( sRGBCol , 2.2) ;
}

float approximationLinearToSRGB (float linearCol)
{
    return pow (linearCol, 1.0 / 2.2);
}

float accurateSRGBToLinear(float sRGBCol)
{
    float linearRGBLo = sRGBCol / 12.92;
    float linearRGBHi = pow (( sRGBCol + 0.055) / 1.055 , 2.4);
    float linearRGB = (sRGBCol <= 0.04045) ? linearRGBLo : linearRGBHi ;
    return linearRGB ;
}

float accurateLinearToSRGB(float linearCol)
{
    float sRGBLo = linearCol * 12.92;
    float sRGBHi = ( pow( abs ( linearCol ) , 1.0/2.4) * 1.055) - 0.055;
    float sRGB = (linearCol <= 0.0031308) ? sRGBLo : sRGBHi;
    return sRGB;
}

vec4 approximationSRgbToLinear(vec4 sRgb)
{
    return vec4(
        approximationSRgbToLinear(sRgb.r),
        approximationSRgbToLinear(sRgb.g),
        approximationSRgbToLinear(sRgb.b),
        sRgb.a
    );
}

vec4 approximationLinearToSRGB(vec4 rgb)
{
    return vec4(
        approximationLinearToSRGB(rgb.r),
        approximationLinearToSRGB(rgb.g),
        approximationLinearToSRGB(rgb.b),
        rgb.a
    );
}

vec4 accurateSRGBToLinear(vec4 sRgb)
{
    return vec4(
        accurateSRGBToLinear(sRgb.r),
        accurateSRGBToLinear(sRgb.g),
        accurateSRGBToLinear(sRgb.b),
        sRgb.a
    );
}

vec4 accurateLinearToSRGB(vec4 rgb)
{
    return vec4(
        accurateLinearToSRGB(rgb.r),
        accurateLinearToSRGB(rgb.g),
        accurateLinearToSRGB(rgb.b),
        rgb.a
    );
}

// --- PBR Material --- //

struct Material
{
    vec4 baseColor; // linear RGB [0..1]
    vec4 emissive; // linear RGB [0..1] + exposure compensation
    float f0; // [0..1]
    float metallic; // [0..1]
    float perceptualRoughness; // [0..1]
    float ambientOcclusion; // [0..1]
    uint baseColorTexture;
    uint emissiveTexture;
    uint oclusionMetallicRoughnessTexture;
    uint normalsTexture;
    uint ambientOcclusionTexture;
    uint pad0;
    uint pad1;
    uint pad2;
};
RegisterBuffer(std430, readonly, MaterialBuffer, {
    Material materials[];
});
#define GetMaterials(bufferHandle) GetResource(MaterialBuffer, bufferHandle).materials

vec4 GetBaseColor(Material material, vec2 fragTexCoord)
{
    if (material.baseColorTexture < MAX_DESCRIPTOR_COUNT)
    {
        vec4 sRgbColor = texture(GetTexture2D(material.baseColorTexture), fragTexCoord);
        return material.baseColor * accurateSRGBToLinear(sRgbColor);
    }
    return material.baseColor;
}

// rgb = color, w = exposure compensation
vec4 GetEmissive(Material material, vec2 fragTexCoord)
{
    if (material.emissiveTexture < MAX_DESCRIPTOR_COUNT)
    {
        vec4 sRgbColor = texture(GetTexture2D(material.emissiveTexture), fragTexCoord);
        return material.emissive * accurateSRGBToLinear(sRgbColor);
    }
    return material.emissive;
}

vec3 GetNormal(Material material, vec3 fragPos, vec2 fragTexCoord, vec3 fragNormal)
{
	// Perturb normal, see http://www.thetenthplanet.de/archives/1180
    vec3 tangentNormal;
    if (material.normalsTexture < MAX_DESCRIPTOR_COUNT)
    {
        tangentNormal = texture(GetTexture2D(material.normalsTexture), fragTexCoord).xyz * 2.0 - 1.0;
    }
    else
    {
        return fragNormal;
    }

    vec3 q1 = dFdx(fragPos);
	vec3 q2 = dFdy(fragPos);
	vec2 st1 = dFdx(fragTexCoord);
	vec2 st2 = dFdy(fragTexCoord);

	vec3 N = normalize(fragNormal);
	vec3 T = normalize(q1 * st2.t - q2 * st1.t);
	vec3 B = -normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * tangentNormal);
}

vec3 GetOcclusionRoughnessMetallic(Material material, vec2 fragTexCoord)
{
    if (material.oclusionMetallicRoughnessTexture < MAX_DESCRIPTOR_COUNT)
    {
        vec4 result = texture(GetTexture2D(material.oclusionMetallicRoughnessTexture), fragTexCoord);
        return vec3(material.ambientOcclusion * result.r, material.perceptualRoughness * result.g, material.metallic * result.b);
    }
    return vec3(material.ambientOcclusion, material.perceptualRoughness, material.metallic);
}

struct RemappedMaterial
{
    vec3 normal;
    vec3 baseColor; // linear RGB [0..1]
    vec3 diffuseColor;
    vec4 emissive; // linear RGB [0..1] + exposure compensation
    vec3 f0; // [0..1]
    float metallic; // [0..1]
    float roughness; // [0..1]
    float occlusion; // [0..1]
};

vec3 RemapBaseColor(vec3 baseColor, float metallic)
{
    return (1.0 - metallic) * baseColor.rgb;
}

// f0 = 0.16 * reflectance ^ 2
vec3 RemapReflectance(float f0, float metallic, vec3 baseColor)
{
    return vec3(f0 * (1.0 - metallic) + baseColor * metallic);
}

// Perceptually linear roughness to roughness
float RemapRoughness(float perceptualRoughness)
{
    float roughness = clamp(perceptualRoughness, 0.089, 1.0);
    return roughness * roughness;
}

RemappedMaterial RemapMaterial(Material material, vec3 fragPos, vec2 fragTexCoord, vec3 fragNormal)
{
    RemappedMaterial remappedMaterial;

    remappedMaterial.normal = GetNormal(material, fragPos, fragTexCoord, fragNormal);
    remappedMaterial.baseColor = GetBaseColor(material, fragTexCoord).rgb;
    remappedMaterial.emissive = GetEmissive(material, fragTexCoord);

    vec3 occlusionRoughnessMetallic = GetOcclusionRoughnessMetallic(material, fragTexCoord);
    remappedMaterial.occlusion = occlusionRoughnessMetallic.r;
    remappedMaterial.roughness = RemapRoughness(occlusionRoughnessMetallic.g);
    remappedMaterial.metallic = occlusionRoughnessMetallic.b;

    remappedMaterial.diffuseColor = RemapBaseColor(remappedMaterial.baseColor, remappedMaterial.metallic);

    remappedMaterial.f0 = RemapReflectance(material.f0,
        remappedMaterial.metallic,
        remappedMaterial.baseColor);

    return remappedMaterial;
}

// --- BRDF --- //

// BRDF implementation reference from [Google Filament]
// https://google.github.io/filament/Filament.md.html#lighting/imagebasedlights

// todo (hbedard): find better name
struct PBRInfo
{
    vec3 v;
    vec3 n;
    vec3 l;
    vec3 h;
    float NoV;
    float NoH;
    float NoL;
    float HoL;
};

struct BRDFResult
{
    vec3 Fd;
    vec3 F;
    float D;
    float G;
    vec3 Fr;
    vec3 Fr_Fd;
};

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

vec3 F_Schlick(float NoL, vec3 f0)
{
    return f0 + (1.0 - f0) * pow((1.0 - NoL), 5.0);
}

// G_2(l, v) / (4 * |dot(n,l)| * |dot(n,v)|) <-- includes this term
float G_SmithGGXCorrelated(float NoV, float NoL, float a)
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

BRDFResult BRDF(PBRInfo pbr, RemappedMaterial material)
{
    BRDFResult res;

    // Specular
    res.D = D_GGX(pbr.NoH, material.roughness);
    res.F = F_Schlick(pbr.HoL, material.f0);
    res.G = G_SmithGGXCorrelated(pbr.NoV, pbr.NoL, material.roughness);
    res.Fr = (res.D * res.G) * res.F;

    // Diffuse
    res.Fd = (1.0 - res.F) * material.occlusion * material.diffuseColor * Fd_Lambert();

    // combined
    res.Fr_Fd = res.Fr + res.Fd;

    return res;
}

// --- Lights --- //

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

vec3 GetLightDirection(Light light, vec3 fragPos)
{
    vec3 l = normalize(light.position - fragPos);
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    { 
        return normalize(-light.direction);
    }
    return l;
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

vec3 EvaluatePunctualLight(PBRInfo pbr, Light light, vec3 brdf)
{
    float attenuation = 1.0; // no attenuation for directional lights
    if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT)
    {
        attenuation = ComputeSquareFalloff(pbr.l, light.falloffRadius);

        if (light.type == LIGHT_TYPE_SPOT)
        {
            attenuation = attenuation * ComputeSpotAngleAttenuation(
                pbr.l,
                light.direction,
                light.cosInnerAngle,
                light.cosOuterAngle);
        }
    }
    return (brdf * light.intensity * attenuation * pbr.NoL) * light.color;
}

// --- Exposure --- //

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
    vec3 fragPos, vec2 fragTexCoord, vec3 fragNormal, vec3 viewPosition,
    uint materialBuffer, uint materialHandle,
    uint lightBuffer, uint lightCount,
    uint shadowBuffer,
    View view)
{
    Material rawMaterial = GetMaterials(materialBuffer)[materialHandle];
    RemappedMaterial material = RemapMaterial(rawMaterial, fragPos, fragTexCoord, fragNormal);

    PBRInfo pbr;
    pbr.n = material.normal;
    pbr.v = normalize(viewPosition - fragPos);

    BRDFResult brdf;
    vec3 l0 = vec3(0.0);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex)
    {
        Light light = GetLights(lightBuffer)[lightIndex];
        pbr.l = GetLightDirection(light, fragPos);
        pbr.h = normalize(pbr.v + pbr.l);
        pbr.NoV = abs(dot(pbr.n, pbr.v)) + 1e-5;
        pbr.NoH = clamp(dot(pbr.n, pbr.h), 0.0, 1.0);
        pbr.NoL = clamp(dot(pbr.n, pbr.l), 0.0, 1.0);
        pbr.HoL = clamp(dot(pbr.h, pbr.l), 0.0, 1.0);
        brdf = BRDF(pbr, material);
        vec3 lightResult = EvaluatePunctualLight(pbr, light, brdf.Fr_Fd);

        // Shadows
        float shadow = 0.0;
        if (light.type == LIGHT_TYPE_DIRECTIONAL)
        {
            shadow = ComputeShadow(light, fragPos, pbr.n, shadowBuffer);
        }

        l0 = l0 + (1.0 - shadow) * lightResult;
    }

    switch (view.debugInput)
    {
        case 1: // baseColor
            return material.baseColor;
        case 2: // diffuseColor
            return material.diffuseColor;
        case 3: // normals
            return material.normal;
        case 4: // Occlusion
            return vec3(material.occlusion);
        case 5: // Emissive
            return material.emissive.rgb * material.emissive.a;
        case 6: // Metallic
            return vec3(material.metallic);
        case 7: // Roughness
            return vec3(material.roughness);
    };

    switch (view.debugEquation)
    {
        case 1: // diffuse
            return brdf.Fd;
        case 2: // F
            return brdf.F;
        case 3: // G
            return vec3(brdf.G) * 4 * (abs(pbr.NoL) * abs(pbr.NoV));
        case 4: // D
            return vec3(brdf.D);
        case 5: // Specular
            return brdf.Fr;
    };

    // todo (hbedard): use IBL for indirect lighting
    // float ev100 = EV100FromExposureSettings(2.8, 0.5, 800.0);
    // float exposure = exposureFromEV100(ev100);
    vec3 color = l0;
    color += material.emissive.rgb * material.emissive.a;//* pow(2.0, ev100 + emissive.w - 3.0);
    color *= view.exposure;
    vec3 ambient = vec3(0.03) * material.baseColor * material.occlusion;
    color += ambient;
    return vec3(color);
}
