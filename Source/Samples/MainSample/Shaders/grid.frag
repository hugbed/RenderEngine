#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 nearPoint;
layout(location = 2) in vec3 farPoint;
layout(location = 3) in mat4 fragView;
layout(location = 7) in mat4 fragProj;

layout(location = 0) out vec4 outColor;

float checkerboard(vec2 R, float scale) {
	return float((
		int(floor(R.x / scale)) +
		int(floor(R.y / scale))
	) % 2);
}

vec4 grid(vec3 R, float scale) {
    // Pick a coordinate to visualize in a grid
    vec2 coord = R.xz * scale;

    // Compute anti-aliased world-space grid lines
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1);
    float minimumx = min(derivative.x, 1);

    vec4 color = vec4(0.2, 0.2, 0.2, 1.0 - min(line, 1.0));

    // z axis
    if(R.x > -0.1 * minimumx && R.x < 0.1 * minimumx)
        color.z = 1.0;

    //x axis
    if(R.z > -0.1 * minimumz && R.z < 0.1 * minimumz)
        color.x = 1.0;

    return color;
}

float computeDepth(vec3 pos) {
	vec4 clip_space_pos = fragProj * fragView * vec4(pos.xyz, 1.0);
	return (clip_space_pos.z / clip_space_pos.w);
}

float computeLinearDepth(vec3 pos) {
    float near = 0.1;
    float far = 100;
	vec4 clip_space_pos = fragProj * fragView * vec4(pos.xyz, 1.0);
	float clip_space_depth = (clip_space_pos.z / clip_space_pos.w) * 2.0 - 1.0;
    float linearDepth = (2.0 * near * far) / (far + near - clip_space_depth * (far - near));
	return linearDepth / far;
}

void main() {
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);
	vec3 pos3D = nearPoint + t * (farPoint - nearPoint);

    vec4 c = (grid(pos3D, 10) + grid(pos3D, 1) ) * float(t > 0);

    float depth = computeDepth(pos3D);
    float linearDepth = computeLinearDepth(pos3D);
    float spotLight = max(0, (0.5 - linearDepth));

    gl_FragDepth = depth;
    outColor = c;
    outColor.a *= spotLight;
}
