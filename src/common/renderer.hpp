/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#pragma once
#include <glm/mat4x4.hpp>

struct GLFWwindow;

struct ViewSettings
{
	float pitch = 0.0f;
	float yaw = 0.0f;
	float distance;
	float fov;
};

struct SceneSettings
{
	float pitch = 0.0f;
	float yaw = 0.0f;

	static const int NumLights = 3;
	struct Light {
		glm::vec3 direction;
		glm::vec3 radiance;
		bool enabled = false;
	} lights[NumLights];
};

class RendererInterface
{
public:
	virtual ~RendererInterface() = default;

	virtual GLFWwindow* initialize(int width, int height, int maxSamples) = 0;
	virtual void shutdown() = 0;
	virtual void setup() = 0;
	virtual void render(GLFWwindow* window, const ViewSettings& view, const SceneSettings& scene) = 0;
};
