#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

// This implementation is based on "Real Shading in Unreal Engine 4" SIGGRAPH 2013 course notes by Epic Games.
// See: http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

const float PI = 3.141592;
const float Epsilon = 0.001;

// Light parameters.
const vec3 Li = vec3(1.0, 0.0, 0.0);
const vec3 Lradiance = vec3(1.0);

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
vec3 fresnelSchlick(vec3 F0, vec3 Lh, vec3 Lo)
{
	float cosLhLo = max(0.0, dot(Lh, Lo));
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosLhLo, 5.0);
}

void main()
{
	// Sample input textures to get shading model params.
	vec3 albedo = texture2D(albedoTexture, vin.texcoord).rgb;
	float metalness = texture2D(metalnessTexture, vin.texcoord).r;
	float roughness = texture2D(roughnessTexture, vin.texcoord).r;

	// Outgoing light direction (vector from world-space fragment position to the "eye").
	vec3 Lo = normalize(eyePosition - vin.position);
	// Half-vector between Li and Lo.
	vec3 Lh = normalize(Li + Lo);

	// Get current fragment's normal and transform to world space.
	vec3 N = normalize(2.0 * texture2D(normalTexture, vin.texcoord).rgb - 1.0);
	N = normalize(vin.tangentBasis * N);

	// Calculate angles between surface normal and various light vectors.
	float cosLi = max(0.0, dot(N, Li));
	float cosLo = max(0.0, dot(N, Lo));
	float cosLh = max(0.0, dot(N, Lh));

	// Calculate Fresnel factor; for metals use albedo color as F0 (reflectance at normal incidence).
	vec3 F0 = mix(Fdielectric, albedo, metalness);
	vec3 F  = fresnelSchlick(F0, Lh, Lo);

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
	vec3 directLighting = (diffuseBRDF + specularBRDF) * Lradiance * cosLi;

	// Final fragment color (direct lighting only, for now).
	color = vec4(directLighting, 1.0);
}
