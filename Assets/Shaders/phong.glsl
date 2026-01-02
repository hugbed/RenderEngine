// Phong properties
struct PhongProperties {
    vec4 diffuse;
    vec4 specular;
    float shininess;
};

// todo (hbdard): those should be TextureHandle to bindless textures
struct PhongTextures {
    int diffuse;
    int specular;
};

// Lights

const uint LIGHT_TYPE_DIRECTIONAL = 1;
const uint LIGHT_TYPE_POINT = 2;
const uint LIGHT_TYPE_SPOT = 3;

struct Light {
    int type; // see LightType enum

    vec3 position; // point
    vec3 direction; // directional

    // colors
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;

    // spot lights
    float innerCutoff; // spot (cos of the inner angle)
    float outerCutoff; // spot (cos of the outer angle)

    // shadows
    uint shadowIndex;
};

vec4 PhongLighting(
    Light light,
    PhongProperties props,
    vec3 normal, vec3 fragPos, vec3 viewPos,
    float shadow
) {
    vec3 viewDir = normalize(viewPos - fragPos);

    vec3 lightDir = light.type == LIGHT_TYPE_DIRECTIONAL ?
        -light.direction : light.position - fragPos;
    float lightDistance = length(lightDir);
    lightDir /= lightDistance;

    // ambient
    vec4 ambient = light.ambient * props.diffuse;

    // diffuse
    float k_d = max(dot(lightDir, normal), 0.0);
    vec4 diffuse = k_d * light.diffuse * props.diffuse;

    // specular
    vec3 h = normalize(lightDir + viewDir); // blinn-phong specular
    float k_s = pow(max(dot(normal, h), 0.0), props.shininess);
    vec4 specular = k_s * light.specular * props.specular;

    // distance attenuation
    float attenuation = 1.0;
    if (light.type != LIGHT_TYPE_DIRECTIONAL) {
        attenuation = 1.0 / lightDistance;
    }

    // spotlight angle attenuation
    if (light.type == LIGHT_TYPE_SPOT) {
        float cos_theta = dot(-lightDir, normalize(light.direction));
        float epsilon = light.innerCutoff - light.outerCutoff;
        float intensity = clamp((cos_theta - light.outerCutoff) / epsilon, 0.0, 1.0);
        // don't affect ambient
        diffuse *= intensity;
        specular *= intensity;
    }

    return (ambient + (1.0 - shadow) * (diffuse + specular)) * attenuation;
}
