#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

// Physically Based shading model: Lambetrtian diffuse BRDF + Cook-Torrace microfacet specular BRDF + IBL for ambient.

// This implementation is based on "Real Shading in Unreal Engine 4" SIGGRAPH 2013 course notes by Epic Games.
// See: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

const float PI = 3.141592;
const float Epsilon = 0.001;

// Light parameters.
const vec3 Li = vec3(1.0, 0.0, 0.0);
const vec3 Lradiance = vec3(2.0);

// Constant normal incidence Fresnel factor for all dielectrics.
const vec3 Fdielectric = vec3(0.04);

in Vertex {
	vec3 position;
	vec2 texcoord;
	mat3 tangentBasis;
} vin;

out vec4 color;

layout(location=1) uniform vec3 eyePosition;

layout(binding=0) uniform sampler2D albedoTexture;
layout(binding=1) uniform sampler2D normalTexture;
layout(binding=2) uniform sampler2D metalnessTexture;
layout(binding=3) uniform sampler2D roughnessTexture;
layout(binding=4) uniform samplerCube irradianceTexture;

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
	// Sample input textures to get shading model params.
	vec3 albedo = texture2D(albedoTexture, vin.texcoord).rgb;
	float metalness = texture2D(metalnessTexture, vin.texcoord).r;
	float roughness = texture2D(roughnessTexture, vin.texcoord).r;

	// Outgoing light direction (vector from world-space fragment position to the "eye").
	vec3 Lo = normalize(eyePosition - vin.position);

	// Get current fragment's normal and transform to world space.
	vec3 N = normalize(2.0 * texture2D(normalTexture, vin.texcoord).rgb - 1.0);
	N = normalize(vin.tangentBasis * N);
	
	// Pre-fetch diffuse irradiance at normal direction (for IBL later).
	vec3 irradiance = texture(irradianceTexture, N).rgb;
	
	// Angle between surface normal and outgoing light direction.
	float cosLo = max(0.0, dot(N, Lo));
	
	// Fresnel reflectance at normal incidence (for metals use albedo color).
	vec3 F0 = mix(Fdielectric, albedo, metalness);

	// Direct lighting calculation
	vec3 directLighting;
	{
		// Half-vector between Li and Lo.
		vec3 Lh = normalize(Li + Lo);

		// Calculate angles between surface normal and various light vectors.
		float cosLi = max(0.0, dot(N, Li));
		float cosLh = max(0.0, dot(N, Lh));

		// Calculate Fresnel term for direct lighting. 
		vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
		// Calculate normal distribution for specular BRDF.
		float D = ndfGGX(cosLh, roughness);
		// Calculate geometric attenuation for specular BRDF.
		float G = gaSchlickGGX(cosLi, cosLo, roughness);

		// Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
		// Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
		// To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
		vec3 kd = mix(vec3(0.0), vec3(1.0) - F, metalness);

		// Lambert diffuse BRDF.
		vec3 diffuseBRDF = kd * (albedo / PI);

		// Cook-Torrance specular microfacet BRDF.
		vec3 specularBRDF = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

		// Total direct lighting contribution.
		directLighting = (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
	}

	// Diffuse ambient lighting (IBL).
	vec3 ambientLighting;
	{
		// Calculate Fresnel term for ambient lighting.
		// Since we use pre-filtered cubemap(s) and irradiance is coming from many directions
		// use cosLo instead of angle with light's half-vector (cosLh above).
		// See: https://seblagarde.wordpress.com/2011/08/17/hello-world/
		vec3 F = fresnelSchlick(F0, cosLo);

		// Get diffuse contribution factor (as with direct lighting).
		vec3 kd = mix(vec3(0.0), vec3(1.0) - F, metalness);

		// Irradiance map is already pre-filtered with Lambertian BRDF (assuming white albedo of 1.0),
		// no need to scale by 1/PI.
		ambientLighting = kd * albedo * irradiance;
	}

	// Final fragment color.
	color = vec4(directLighting + ambientLighting, 1.0);
}
