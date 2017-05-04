#version 430
// Physically Based Rendering
// Copyright (c) 2017 Michał Siejak

const float PI=3.141592;

in vec3 localPosition;
out vec4 color;

layout(binding=0) uniform sampler2D envTexture;

void main()
{
	// Environment map probe direction vector.
	vec3 envVector = normalize(localPosition);

	// Convert direction vector to latlong coordinates.
	float phi   = atan(envVector.z, envVector.x);
	float theta = acos(envVector.y);
	vec2 uv     = vec2(phi/(2.0*PI), theta/PI);

	// Sample environment texture.
	color = texture2D(envTexture, uv);
}
