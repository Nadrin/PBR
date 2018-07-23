#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Generates vertices of a triangle that covers the whole screen in clip space.

#if !VULKAN
layout(location=0) out vec2 screenPosition;
#endif

void main()
{
#if VULKAN
	if(gl_VertexIndex == 0) {
		gl_Position = vec4(1.0, 1.0, 0.0, 1.0);
	}
	else if(gl_VertexIndex == 1) {
		gl_Position = vec4(1.0, -3.0, 0.0, 1.0);
	}
	else /* if(gl_VertexIndex == 2) */ {
		gl_Position = vec4(-3.0, 1.0, 0.0, 1.0);
	}
#else
	if(gl_VertexID == 0) {
		screenPosition = vec2(1.0, 2.0);
		gl_Position = vec4(1.0, 3.0, 0.0, 1.0);
	}
	else if(gl_VertexID == 1) {
		screenPosition = vec2(-1.0, 0.0);
		gl_Position = vec4(-3.0, -1.0, 0.0, 1.0);
	}
	else /* if(gl_VertexID == 2) */ {
		screenPosition = vec2(1.0, 0.0);
		gl_Position = vec4(1.0, -1.0, 0.0, 1.0);
	}
#endif
}
