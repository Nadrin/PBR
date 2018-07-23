#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Tone-mapping & gamma correction.

const float gamma     = 2.2;
const float exposure  = 1.0;
const float pureWhite = 1.0;

#if VULKAN
layout(input_attachment_index=0, set=0, binding=0) uniform subpassInput sceneColor;
#else
layout(location=0) in  vec2 screenPosition;
layout(binding=0) uniform sampler2D sceneColor;
#endif // VULKAN

layout(location=0) out vec4 outColor;

void main()
{
#if VULKAN
	vec3 color = subpassLoad(sceneColor).rgb * exposure;
#else
	vec3 color = texture(sceneColor, screenPosition).rgb * exposure;
#endif // VULKAN

	// Reinhard tonemapping operator.
	// see: "Photographic Tone Reproduction for Digital Images", eq. 4
	float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float mappedLuminance = (luminance * (1.0 + luminance/(pureWhite*pureWhite))) / (1.0 + luminance);

	// Scale color by ratio of average luminances.
	vec3 mappedColor = (mappedLuminance / luminance) * color;

	// Gamma correction.
	outColor = vec4(pow(mappedColor, vec3(1.0/gamma)), 1.0);
}
