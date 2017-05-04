#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

in Vertex {
	vec3 normal;
	vec2 texcoord;
} vin;

out vec4 color;

void main()
{
	// Placeholder, for now.
	vec3 N = normalize(vin.normal);
	color = vec4(N, 1.0);
}
