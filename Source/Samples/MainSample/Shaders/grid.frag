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

vec4 grid(vec3 R, float scale, bool drawAxis) {
    // Pick a coordinate to visualize in a grid
    vec2 coord = R.xz * scale;

    float mult = 1;

    if(scale == 1)
        mult = 2;

    // Compute anti-aliased world-space grid lines
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / (fwidth(coord) * mult);
    float line = min(grid.x, grid.y); 

    vec4 color = vec4(0.2, 0.2, 0.2, 1.0 - min(line, 1.0));

    // z axis
    if(coord.x > -0.001 && coord.x < 0.001)
        color.z = 1.0;

    //x axis
    if(coord.y > -0.001 && coord.y < 0.001)
        color.x = 1.0;

    return color;
}

float computeDepth(vec3 pos) {
	vec4 clip_space_pos = fragProj * fragView * vec4(pos.xyz, 1.0);
	float clip_space_depth = clip_space_pos.z / clip_space_pos.w;
	return clip_space_depth;
}

void main() {
    float t = -nearPoint.y / (farPoint.y - nearPoint.y);
	vec3 R = nearPoint + t * (farPoint - nearPoint);

    vec4 c = grid(R, 10, true) + grid(R, 1, true);

    c = c * float(t > 0);
	float spotlight = min(1.0, 5.0 - length(R.xyz));

    float depth = computeDepth(R);

    gl_FragDepth = depth;
	outColor = c * spotlight; 
}
