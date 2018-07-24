// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Texture mip level downsampling with linear filtering (used in manual mip chain generation).

static const float gamma = 2.2;

Texture2D inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void downsample_linear(uint2 ThreadID : SV_DispatchThreadID)
{
	int3 sampleLocation = int3(2 * ThreadID.x, 2 * ThreadID.y, 0);
	float4 gatherValue = 
		inputTexture.Load(sampleLocation, int2(0, 0)) +
		inputTexture.Load(sampleLocation, int2(1, 0)) +
		inputTexture.Load(sampleLocation, int2(0, 1)) +
		inputTexture.Load(sampleLocation, int2(1, 1));
	outputTexture[ThreadID] = 0.25 * gatherValue;
}

[numthreads(8, 8, 1)]
void downsample_gamma(uint2 ThreadID : SV_DispatchThreadID)
{
	int3 sampleLocation = int3(2 * ThreadID.x, 2 * ThreadID.y, 0);

	float4 value0 = inputTexture.Load(sampleLocation, int2(0, 0));
	float4 value1 = inputTexture.Load(sampleLocation, int2(1, 0));
	float4 value2 = inputTexture.Load(sampleLocation, int2(0, 1));
	float4 value3 = inputTexture.Load(sampleLocation, int2(1, 1));

	float4 gatherValue;
	gatherValue.rgb = pow(value0.rgb, gamma) + pow(value1.rgb, gamma) + pow(value2.rgb, gamma) + pow(value3.rgb, gamma);
	gatherValue.a   = value0.a + value1.a + value2.a + value3.a;

	outputTexture[ThreadID] = float4(pow(0.25 * gatherValue.rgb, 1.0/gamma), 0.25 * gatherValue.a);
}
