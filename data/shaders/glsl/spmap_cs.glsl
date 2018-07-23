#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Pre-filters environment cube map using GGX NDF importance sampling.
// Part of specular IBL split-sum approximation.

const float PI = 3.141592;
const float TwoPI = 2 * PI;
const float Epsilon = 0.00001;

const uint NumSamples = 1024;
const float InvNumSamples = 1.0 / float(NumSamples);

// In Vulkan whole mip tail is bound to the descriptor set and appropriate mip level is selected via a push constant.
#if VULKAN
layout(constant_id=0) const int NumMipLevels = 1;
layout(set=0, binding=0) uniform samplerCube inputTexture;
layout(set=0, binding=2, rgba16f) restrict writeonly uniform imageCube outputTexture[NumMipLevels];
#else
// In OpenGL only a single mip level is bound.
const int NumMipLevels = 1;
layout(binding=0) uniform samplerCube inputTexture;
layout(binding=0, rgba16f) restrict writeonly uniform imageCube outputTexture[NumMipLevels];
#endif // VULKAN

#if VULKAN
layout(push_constant) uniform PushConstants
{
	// Output texture mip level (without base mip level).
	int level;
	// Roughness value to pre-filter for.
	float roughness;
} pushConstants;

#define PARAM_LEVEL     pushConstants.level
#define PARAM_ROUGHNESS pushConstants.roughness
#else
// Roughness value to pre-filter for.
layout(location=0) uniform float roughness;

#define PARAM_LEVEL     0
#define PARAM_ROUGHNESS roughness
#endif // VULKAN

// Compute Van der Corput radical inverse
// See: http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverse_VdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// Sample i-th point from Hammersley point set of NumSamples points total.
vec2 sampleHammersley(uint i)
{
	return vec2(i * InvNumSamples, radicalInverse_VdC(i));
}

// Importance sample GGX normal distribution function for a fixed roughness value.
// This returns normalized half-vector between Li & Lo.
// For derivation see: http://blog.tobias-franke.eu/2014/03/30/notes_on_importance_sampling.html
vec3 sampleGGX(float u1, float u2, float roughness)
{
	float alpha = roughness * roughness;

	float cosTheta = sqrt((1.0 - u2) / (1.0 + (alpha*alpha - 1.0) * u2));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta); // Trig. identity
	float phi = TwoPI * u1;

	// Convert to Cartesian upon return.
	return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Calculate normalized sampling direction vector based on current fragment coordinates (gl_GlobalInvocationID.xyz).
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
// See: OpenGL core profile specs, section 8.13.
vec3 getSamplingVector()
{
    vec2 st = gl_GlobalInvocationID.xy/vec2(imageSize(outputTexture[PARAM_LEVEL]));
    vec2 uv = 2.0 * vec2(st.x, 1.0-st.y) - vec2(1.0);

    vec3 ret;
    // Sadly 'switch' doesn't seem to work, at least on NVIDIA.
    if(gl_GlobalInvocationID.z == 0)      ret = vec3(1.0,  uv.y, -uv.x);
    else if(gl_GlobalInvocationID.z == 1) ret = vec3(-1.0, uv.y,  uv.x);
    else if(gl_GlobalInvocationID.z == 2) ret = vec3(uv.x, 1.0, -uv.y);
    else if(gl_GlobalInvocationID.z == 3) ret = vec3(uv.x, -1.0, uv.y);
    else if(gl_GlobalInvocationID.z == 4) ret = vec3(uv.x, uv.y, 1.0);
    else if(gl_GlobalInvocationID.z == 5) ret = vec3(-uv.x, uv.y, -1.0);
    return normalize(ret);
}

// Compute orthonormal basis for converting from tanget/shading space to world space.
void computeBasisVectors(const vec3 N, out vec3 S, out vec3 T)
{
	// Branchless select non-degenerate T.
	T = cross(N, vec3(0.0, 1.0, 0.0));
	T = mix(cross(N, vec3(1.0, 0.0, 0.0)), T, step(Epsilon, dot(T, T)));

	T = normalize(T);
	S = normalize(cross(N, T));
}

// Convert point from tangent/shading space to world space.
vec3 tangentToWorld(const vec3 v, const vec3 N, const vec3 S, const vec3 T)
{
	return S * v.x + T * v.y + N * v.z;
}

layout(local_size_x=32, local_size_y=32, local_size_z=1) in;
void main(void)
{
	// Make sure we won't write past output when computing higher mipmap levels.
	ivec2 outputSize = imageSize(outputTexture[PARAM_LEVEL]);
	if(gl_GlobalInvocationID.x >= outputSize.x || gl_GlobalInvocationID.y >= outputSize.y) {
		return;
	}
	
	// Solid angle associated with a single cubemap texel at zero mipmap level.
	// This will come in handy for importance sampling below.
	vec2 inputSize = vec2(textureSize(inputTexture, 0));
	float wt = 4.0 * PI / (6 * inputSize.x * inputSize.y);
	
	// Approximation: Assume zero viewing angle (isotropic reflections).
	vec3 N = getSamplingVector();
	vec3 Lo = N;
	
	vec3 S, T;
	computeBasisVectors(N, S, T);

	vec3 color = vec3(0);
	float weight = 0;

	// Convolve environment map using GGX NDF importance sampling.
	// Weight by cosine term since Epic claims it generally improves quality.
	for(uint i=0; i<NumSamples; ++i) {
		vec2 u = sampleHammersley(i);
		vec3 Lh = tangentToWorld(sampleGGX(u.x, u.y, PARAM_ROUGHNESS), N, S, T);

		// Compute incident direction (Li) by reflecting viewing direction (Lo) around half-vector (Lh).
		vec3 Li = 2.0 * dot(Lo, Lh) * Lh - Lo;

		float cosLi = dot(N, Li);
		if(cosLi > 0.0) {
			// Use Mipmap Filtered Importance Sampling to improve convergence.
			// See: https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html, section 20.4

			float cosLh = max(dot(N, Lh), 0.0);

			// GGX normal distribution function (D term) probability density function.
			// Scaling by 1/4 is due to change of density in terms of Lh to Li (and since N=V, rest of the scaling factor cancels out).
			float pdf = ndfGGX(cosLh, PARAM_ROUGHNESS) * 0.25;

			// Solid angle associated with this sample.
			float ws = 1.0 / (NumSamples * pdf);

			// Mip level to sample from.
			float mipLevel = max(0.5 * log2(ws / wt) + 1.0, 0.0);

			color  += textureLod(inputTexture, Li, mipLevel).rgb * cosLi;
			weight += cosLi;
		}
	}
	color /= weight;

	imageStore(outputTexture[PARAM_LEVEL], ivec3(gl_GlobalInvocationID), vec4(color, 1.0));
}
