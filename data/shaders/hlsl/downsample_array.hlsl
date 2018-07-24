// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Texture mip level downsampling with linear filtering (used in manual mip chain generation).

Texture2DArray inputTexture : register(t0);
RWTexture2DArray<float4> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void downsample_linear(uint3 ThreadID : SV_DispatchThreadID)
{
	int4 sampleLocation = int4(2 * ThreadID.x, 2 * ThreadID.y, ThreadID.z, 0);
	float4 gatherValue = 
		inputTexture.Load(sampleLocation, int3(0, 0, 0)) +
		inputTexture.Load(sampleLocation, int3(1, 0, 0)) +
		inputTexture.Load(sampleLocation, int3(0, 1, 0)) +
		inputTexture.Load(sampleLocation, int3(1, 1, 0));
	outputTexture[ThreadID] = 0.25 * gatherValue;
}
