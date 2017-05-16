/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#include <stdexcept>
#include <GLFW/glfw3.h>

#include "application.hpp"

namespace {
	const int DisplaySizeX = 1024;
	const int DisplaySizeY = 1024;
	const int DisplaySamples = 16;

	const float ViewDistance   = 150.0f;
	const float ViewFOV        = 45.0f;
	const float ViewOrbitSpeed = 1.0f;
	const float ViewZoomSpeed  = 4.0f;
}

Application::Application()
	: m_window(nullptr)
	, m_prevCursorX(0.0)
	, m_prevCursorY(0.0)
{
	if(!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW library");
	}

	m_viewSettings.yaw   = 0.0f;
	m_viewSettings.pitch = 0.0f;
	m_viewSettings.distance = ViewDistance;
	m_viewSettings.fov      = ViewFOV;
}

Application::~Application()
{
	if(m_window) {
		glfwDestroyWindow(m_window);
	}
	glfwTerminate();
}

void Application::run(const std::unique_ptr<RendererInterface>& renderer)
{
	glfwWindowHint(GLFW_RESIZABLE, 0);
	m_window = renderer->initialize(DisplaySizeX, DisplaySizeY, DisplaySamples);

	glfwSetWindowUserPointer(m_window, this);
	glfwSetCursorPosCallback(m_window, Application::mousePositionCallback);
	glfwSetMouseButtonCallback(m_window, Application::mouseButtonCallback);
	glfwSetScrollCallback(m_window, Application::mouseScrollCallback);

	renderer->setup();
	while(!glfwWindowShouldClose(m_window)) {
		renderer->render(m_window, m_viewSettings);
		glfwPollEvents();
	}

	renderer->shutdown();
}

void Application::mousePositionCallback(GLFWwindow* window, double xpos, double ypos)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	if(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
		const double dx = xpos - self->m_prevCursorX;
		const double dy = ypos - self->m_prevCursorY;

		self->m_viewSettings.yaw   += ViewOrbitSpeed * float(dx);
		self->m_viewSettings.pitch += ViewOrbitSpeed * float(dy);

		self->m_prevCursorX = xpos;
		self->m_prevCursorY = ypos;
	}
}
	
void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

	if(action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_1) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwGetCursorPos(window, &self->m_prevCursorX, &self->m_prevCursorY);
	}
	if(action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_1) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}
	
void Application::mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	self->m_viewSettings.distance += ViewZoomSpeed * float(-yoffset);
}
