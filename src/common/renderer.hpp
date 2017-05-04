/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#pragma once
#include <glm/mat4x4.hpp>

struct GLFWwindow;

struct ViewSettings
{
	float pitch;
	float yaw;
	float distance;
	float fov;
};

class RendererInterface
{
public:
	virtual ~RendererInterface() = default;

	virtual GLFWwindow* initialize(int width, int height, int samples) = 0;
	virtual void shutdown() = 0;
	virtual void setup() = 0;
	virtual void render(GLFWwindow* window, const ViewSettings& view) = 0;
};
