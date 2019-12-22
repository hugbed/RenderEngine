// Phong material
struct PhongMaterial {
    vec3 diffuse;
    vec3 specular;
    float shininess;
};

// Point light
struct Attenuation {
    float constant;
    float linear;
    float quadratic;
};
struct PointLight {
    vec3 pos;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    Attenuation attenuation;
};

vec3 PhongPointLight(
    PointLight light,
    PhongMaterial material,
    vec3 normal, vec3 fragPos, vec3 viewDir
) {
    // ambient
    vec3 ambient = light.ambient * material.diffuse;

    // diffuse
    vec3 lightDir = normalize(light.pos - fragPos);
    float k = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = k * light.diffuse * material.diffuse;

    // specular
    vec3 lightDirReflect = reflect(-lightDir, normal);
    k = pow(max(dot(viewDir, lightDirReflect), 0.0), material.shininess);
    vec3 specular =  k * light.specular * material.specular;

    // distance attenuation
    Attenuation att = light.attenuation;
    float dist = length(light.pos - fragPos);
    float attenuation = 1.0 / (att.constant + att.linear * dist + att.quadratic * dist*dist);

    return (ambient + diffuse + specular) * attenuation;
}
