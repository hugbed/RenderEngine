struct Attenuation
{
    float constant;
    float linear;
    float quadratic;
};

struct PointLight {
    vec3 pos;
    vec3 colorDiffuse;
    vec3 colorSpecular;
    Attenuation attenuation; // constant, linear quadratic
};

vec3 PhongPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float shininess)
{
    // diffuse
    vec3 lightColor = light.colorDiffuse;
    vec3 lightDir = light.pos - fragPos;
    float dist = length(lightDir);
    lightDir = normalize(lightDir);
    float contribution = max(dot(lightDir, normal), 0.0);
    Attenuation att = light.attenuation;
    float distAttenuation = 1.0 / (att.constant + att.linear * dist + att.quadratic * dist*dist);
    vec3 diffuseColor = contribution * lightColor * distAttenuation;

    // specular
    lightColor = light.colorSpecular;
    vec3 lightDirReflect = reflect(-lightDir, normal);
    contribution = pow(max(dot(viewDir, lightDirReflect), 0.0), shininess);
    vec3 specularColor = contribution * lightColor * distAttenuation;

    return diffuseColor + specularColor;
}
