struct View
{
    mat4 view;
    mat4 proj;
    vec3 pos;
    float exposure;
    uint debugInput;
    uint debugEquation;
};

RegisterUniform(ViewUniforms, { View view; });
