#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Converts equirectangular (lat-long) projection texture into a proper cubemap.

const float PI = 3.141592;
const float TwoPI = 2 * PI;

#if VULKAN
layout(set=0, binding=0) uniform sampler2D inputTexture;
layout(set=0, binding=1, rgba16f) restrict writeonly uniform imageCube outputTexture;
#else
layout(binding=0) uniform sampler2D inputTexture;
layout(binding=0, rgba16f) restrict writeonly uniform imageCube outputTexture;
#endif // VULKAN

// Calculate normalized sampling direction vector based on current fragment coordinates (gl_GlobalInvocationID.xyz).
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
// See: OpenGL core profile specs, section 8.13.
vec3 getSamplingVector()
{
    vec2 st = gl_GlobalInvocationID.xy/vec2(imageSize(outputTexture));
    vec2 uv = 2.0 * vec2(st.x, 1.0-st.y) - vec2(1.0);

    vec3 ret;
	// Select vector based on cubemap face index.
    // Sadly 'switch' doesn't seem to work, at least on NVIDIA.
    if(gl_GlobalInvocationID.z == 0)      ret = vec3(1.0,  uv.y, -uv.x);
    else if(gl_GlobalInvocationID.z == 1) ret = vec3(-1.0, uv.y,  uv.x);
    else if(gl_GlobalInvocationID.z == 2) ret = vec3(uv.x, 1.0, -uv.y);
    else if(gl_GlobalInvocationID.z == 3) ret = vec3(uv.x, -1.0, uv.y);
    else if(gl_GlobalInvocationID.z == 4) ret = vec3(uv.x, uv.y, 1.0);
    else if(gl_GlobalInvocationID.z == 5) ret = vec3(-uv.x, uv.y, -1.0);
    return normalize(ret);
}

layout(local_size_x=32, local_size_y=32, local_size_z=1) in;
void main(void)
{
	vec3 v = getSamplingVector();

	// Convert Cartesian direction vector to spherical coordinates.
	float phi   = atan(v.z, v.x);
	float theta = acos(v.y);

	// Sample equirectangular texture.
	vec4 color = texture(inputTexture, vec2(phi/TwoPI, theta/PI));

	// Write out color to output cubemap.
	imageStore(outputTexture, ivec3(gl_GlobalInvocationID), color);
}
