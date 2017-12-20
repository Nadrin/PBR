#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

// Environment skybox: Vertex program.
layout(std140, binding=0) uniform TransformUniforms
{
	mat4 viewProjectionMatrix;
	mat4 skyProjectionMatrix;
	mat4 sceneRotationMatrix;
};

layout(location=0) in vec3 position;

out vec3 localPosition;

void main()
{
	localPosition = position.xyz;
	gl_Position   = skyProjectionMatrix * vec4(position, 1.0);
}
