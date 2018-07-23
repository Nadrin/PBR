#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Environment skybox: Fragment program.

layout(location=0) in vec3 localPosition;
layout(location=0) out vec4 color;

#if VULKAN
layout(set=1, binding=0) uniform samplerCube envTexture;
#else
layout(binding=0) uniform samplerCube envTexture;
#endif // VULKAN

void main()
{
	vec3 envVector = normalize(localPosition);
	color = textureLod(envTexture, envVector, 0);
}
