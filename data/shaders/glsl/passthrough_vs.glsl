#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;

out vec2 screenPosition;

void main()
{
	screenPosition = texcoord;
	gl_Position    = vec4(position, 0.0, 1.0);
}
