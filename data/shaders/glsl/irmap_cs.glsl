#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Computes diffuse irradiance cubemap convolution for image-based lighting.
// Uses quasi Monte Carlo sampling with Hammersley sequence.

const float PI = 3.141592;
const float TwoPI = 2 * PI;
const float Epsilon = 0.00001;

const uint NumSamples = 64 * 1024;
const float InvNumSamples = 1.0 / float(NumSamples);

#if VULKAN
layout(set=0, binding=0) uniform samplerCube inputTexture;
layout(set=0, binding=1, rgba16f) restrict writeonly uniform imageCube outputTexture;
#else
layout(binding=0) uniform samplerCube inputTexture;
layout(binding=0, rgba16f) restrict writeonly uniform imageCube outputTexture;
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

// Uniformly sample point on a hemisphere.
// Cosine-weighted sampling would be a better fit for Lambertian BRDF but since this
// compute shader runs only once as a pre-processing step performance is not *that* important.
// See: "Physically Based Rendering" 2nd ed., section 13.6.1.
vec3 sampleHemisphere(float u1, float u2)
{
	const float u1p = sqrt(max(0.0, 1.0 - u1*u1));
	return vec3(cos(TwoPI*u2) * u1p, sin(TwoPI*u2) * u1p, u1);
}

// Calculate normalized sampling direction vector based on current fragment coordinates (gl_GlobalInvocationID.xyz).
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
// See: OpenGL core profile specs, section 8.13.
vec3 getSamplingVector()
{
    vec2 st = gl_GlobalInvocationID.xy/vec2(imageSize(outputTexture));
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
	vec3 N = getSamplingVector();
	
	vec3 S, T;
	computeBasisVectors(N, S, T);

	// Monte Carlo integration of hemispherical irradiance.
	// As a small optimization this also includes Lambertian BRDF assuming perfectly white surface (albedo of 1.0)
	// so we don't need to normalize in PBR fragment shader (so technically it encodes exitant radiance rather than irradiance).
	vec3 irradiance = vec3(0);
	for(uint i=0; i<NumSamples; ++i) {
		vec2 u  = sampleHammersley(i);
		vec3 Li = tangentToWorld(sampleHemisphere(u.x, u.y), N, S, T);
		float cosTheta = max(0.0, dot(Li, N));

		// PIs here cancel out because of division by pdf.
		irradiance += 2.0 * textureLod(inputTexture, Li, 0).rgb * cosTheta;
	}
	irradiance /= vec3(NumSamples);

	imageStore(outputTexture, ivec3(gl_GlobalInvocationID), vec4(irradiance, 1.0));
}
