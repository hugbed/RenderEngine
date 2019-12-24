// Phong material
struct PhongMaterial {
    vec3 diffuse;
    vec3 specular;
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
    vec3 pos;
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    Attenuation attenuation;
};

vec3 PhongLighting(
    Light light,
    PhongMaterial material,
    vec3 normal, vec3 fragPos, vec3 viewDir
) {
    vec3 lightDir = light.type == LIGHT_TYPE_POINT ?
        normalize(light.pos - fragPos) :
        normalize(-light.direction);

    // ambient
    vec3 ambient = light.ambient * material.diffuse;

    // diffuse
    float k = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = k * light.diffuse * material.diffuse;

    // specular (apply proportional to diffuse intensity k)
    vec3 specular = vec3(0.0);
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

    return (ambient + diffuse + specular) * attenuation;
}
