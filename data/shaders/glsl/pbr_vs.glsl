#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 tangent;
layout(location=3) in vec3 bitangent;
layout(location=4) in vec2 texcoord;

layout(location=0) uniform mat4 viewProjMatrix;
layout(location=1) uniform mat4 viewMatrix;

out Vertex
{
	vec3 normal;
	vec2 texcoord;
} vout;

void main()
{
	vout.texcoord = texcoord;
	vout.normal   = vec3(viewMatrix * vec4(normal, 0.0));
	gl_Position   = viewProjMatrix * vec4(position, 1.0);
}
