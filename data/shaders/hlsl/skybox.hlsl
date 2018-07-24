// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Environment skybox.

cbuffer TransformConstants : register(b0)
{
	float4x4 viewProjectionMatrix;
	float4x4 skyProjectionMatrix;
	float4x4 sceneRotationMatrix;
};

struct PixelShaderInput
{
	float3 localPosition : POSITION;
	float4 pixelPosition : SV_POSITION;
};

TextureCube envTexture : register(t0);
SamplerState defaultSampler : register(s0);

// Vertex shader
PixelShaderInput main_vs(float3 position : POSITION)
{
	PixelShaderInput vout;
	vout.localPosition = position;
	vout.pixelPosition = mul(skyProjectionMatrix, float4(position, 1.0));
	return vout;
}

// Pixel shader
float4 main_ps(PixelShaderInput pin) : SV_Target
{
	float3 envVector = normalize(pin.localPosition);
	return envTexture.SampleLevel(defaultSampler, envVector, 0);
}
