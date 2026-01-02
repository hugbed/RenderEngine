// assumes:
// #include "bindless.glsl"
// layout(set = 1, binding = 0) uniform DrawParameters {
//   ...
//   uint shadows;
// } uDrawParams;

 // todo (hbedard): add this modification on the code side
struct MaterialShadowData
{
    mat4 transform;
    uint shadowMapTextureHandle;
    uint pad0; uint pad1; uint pad2;
};

RegisterBuffer(std430, readonly, MaterialShadowDataBuffer, {
    MaterialShadowData shadowData[];
});

#define GetShadow() GetResource(MaterialShadowDataBuffer, uDrawParams.shadows).shadowData
#define GetShadowMap(shadowIndex) uGlobalTextures2D[GetShadow()[shadowIndex].shadowMapTextureHandle]

// todo (hbedard): actually put the textures in the global array and add the handle

/// 1.0 means shadow, 0.0 no shadow
float ComputeShadow(Light light, vec3 fragPos, vec3 normal)
{
    // todo: implement shadows for other light types
    if (light.type != LIGHT_TYPE_DIRECTIONAL)
        return 0.0;

    vec4 fragLightPos = GetShadow()[light.shadowIndex].transform * vec4(fragPos, 1.0);

    vec3 lightDir = normalize(light.pos - fragPos);

    float currentDepth = fragLightPos.z / fragLightPos.w;
    // no shadow outside the light's far plane
    if (currentDepth > 1.0)
        return 0.0;

    // fragment in light clip space + from [-1, 1] to [0, 1]
    vec2 mapCoord = 0.5 * (fragLightPos.xy / fragLightPos.w) + 0.5;

    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);

    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(GetShadowMap(light.shadowIndex), 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float depth = texture(GetShadowMap(light.shadowIndex), mapCoord + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > depth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}
