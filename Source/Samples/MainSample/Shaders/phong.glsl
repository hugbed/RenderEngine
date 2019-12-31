// Phong material
struct PhongMaterial {
    vec4 diffuse;
    vec4 specular;
    float shininess;
};

// Lights

const uint LIGHT_TYPE_DIRECTIONAL = 1;
const uint LIGHT_TYPE_POINT = 2;
const uint LIGHT_TYPE_SPOT = 3;

struct Attenuation {
    float constant;
    float linear;
    float quadratic;
};

struct Light {
    int type;
    vec3 pos; // point
    vec3 direction; // directional
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
    float innerCutoff; // spot (cos of the inner angle)
    float outerCutoff; // spot (cos of the outer angle)
    Attenuation attenuation;
};

vec4 PhongLighting(
    Light light,
    PhongMaterial material,
    vec3 normal, vec3 fragPos, vec3 viewDir
) {
    vec3 lightDir = light.type == LIGHT_TYPE_DIRECTIONAL ?
        normalize(-light.direction) :
        normalize(light.pos - fragPos);

    // ambient
    vec4 ambient = light.ambient * material.diffuse;

    // diffuse
    float k = max(dot(lightDir, normal), 0.0);
    vec4 diffuse = k * light.diffuse * material.diffuse;

    // specular (apply proportional to diffuse intensity k)
    vec4 specular = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 lightDirReflect = reflect(-lightDir, normal);
    k *= pow(max(dot(viewDir, lightDirReflect), 0.0), material.shininess);
    specular = k * light.specular * material.specular;

    // distance attenuation
    float attenuation = 1.0;
    if (light.type != LIGHT_TYPE_DIRECTIONAL) {
        Attenuation att = light.attenuation;
        float dist = length(light.pos - fragPos);
        attenuation = 1.0 / (att.constant + att.linear * dist + att.quadratic * dist*dist);
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

    return (ambient + diffuse + specular) * attenuation;
}
