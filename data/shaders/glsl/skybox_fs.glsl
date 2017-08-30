#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

// Environment skybox: Fragment program.

in vec3 localPosition;
out vec4 color;

layout(binding=0) uniform samplerCube envTexture;

void main()
{
	vec3 envVector = normalize(localPosition);
	color = textureLod(envTexture, envVector, 0);
}
