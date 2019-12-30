#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 nearPoint;
layout(location = 2) in vec3 farPoint;
layout(location = 3) in mat4 fragView;
layout(location = 7) in mat4 fragProj;

layout(location = 0) out vec4 outColor;

//--- Set 0 (Scene Uniforms) --- //

// --- Set 1 (Model Uniforms) --- //
// ...

// --- Set 2 (Material Uniforms) --- //

float checkerboard(vec2 R, float scale) {
	return float((
		int(floor(R.x / scale)) +
		int(floor(R.y / scale))
	) % 2);
}

float grid(vec2 R, float scale) {
	return float((
		int(floor(R.x / scale)) +
		int(floor(R.y / scale))
	) % 2);
}

float computeDepth(vec3 pos) {
	vec4 clip_space_pos = fragProj * fragView * vec4(pos.xyz, 1.0);
	float clip_space_depth = clip_space_pos.z / clip_space_pos.w;
	return clip_space_depth;
}

void main() {
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);
	vec3 R = nearPoint + t * (farPoint - nearPoint);
    float c =
		checkerboard(R.xz, 0.1) * 0.3 +
		checkerboard(R.xz, 1) * 0.2 +
		checkerboard(R.xz, 10) * 0.15 +
		0.1;

    c = c * float(t > 0);
	float spotlight = min(1.0, 5.0 - length(R.xyz));

    float depth = computeDepth(R);

    gl_FragDepth = depth;
	outColor = vec4(vec3(c), 1) * spotlight;

    //Z axis
    if(R.x > -0.002 && R.x < 0.002)
    {
        if(R.z > 0)
            outColor = vec4(vec3(0.25, 0.25, (1.0 - R.x) ), 1.0) * spotlight;
        else
            outColor = vec4(vec3(0.25, 0.25, (1.0 - R.x)), 1.0) * spotlight;
    }

    //X axis
    if(R.z > -0.002 && R.z < 0.002)
    {
        if(R.x > 0)
            outColor = vec4(vec3((1.0 - R.z), 0.25, 0.25), 1.0) * spotlight;
        else
            outColor = vec4(vec3((1.0 - R.z), 0.25, 0.25), 1.0) * spotlight;
    }    
}
