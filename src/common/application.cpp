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

	const float ViewDistance = 150.0f;
	const float ViewFOV      = 45.0f;
	const float OrbitSpeed   = 1.0f;
	const float ZoomSpeed    = 4.0f;
}

Application::Application()
	: m_window(nullptr)
	, m_prevCursorX(0.0)
	, m_prevCursorY(0.0)
	, m_mode(InputMode::None)
{
	if(!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW library");
	}

	m_viewSettings.distance = ViewDistance;
	m_viewSettings.fov      = ViewFOV;

	m_sceneSettings.lights[0].direction = glm::normalize(glm::vec3{-1.0f,  0.0f, 0.0f});
	m_sceneSettings.lights[1].direction = glm::normalize(glm::vec3{ 1.0f,  0.0f, 0.0f});
	m_sceneSettings.lights[2].direction = glm::normalize(glm::vec3{ 0.0f, -1.0f, 0.0f});

	m_sceneSettings.lights[0].radiance = glm::vec3{1.0f};
	m_sceneSettings.lights[1].radiance = glm::vec3{1.0f};
	m_sceneSettings.lights[2].radiance = glm::vec3{1.0f};
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
	glfwSetKeyCallback(m_window, Application::keyCallback);

	renderer->setup();
	while(!glfwWindowShouldClose(m_window)) {
		renderer->render(m_window, m_viewSettings, m_sceneSettings);
		glfwPollEvents();
	}

	renderer->shutdown();
}

void Application::mousePositionCallback(GLFWwindow* window, double xpos, double ypos)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	if(self->m_mode != InputMode::None) {
		const double dx = xpos - self->m_prevCursorX;
		const double dy = ypos - self->m_prevCursorY;

		switch(self->m_mode) {
		case InputMode::RotatingScene:
			self->m_sceneSettings.yaw   += OrbitSpeed * float(dx);
			self->m_sceneSettings.pitch += OrbitSpeed * float(dy);
			break;
		case InputMode::RotatingView:
			self->m_viewSettings.yaw   += OrbitSpeed * float(dx);
			self->m_viewSettings.pitch += OrbitSpeed * float(dy);
			break;
		}

		self->m_prevCursorX = xpos;
		self->m_prevCursorY = ypos;
	}
}
	
void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

	const InputMode oldMode = self->m_mode;
	if(action == GLFW_PRESS && self->m_mode == InputMode::None) {
		switch(button) {
		case GLFW_MOUSE_BUTTON_1:
			self->m_mode = InputMode::RotatingView;
			break;
		case GLFW_MOUSE_BUTTON_2:
			self->m_mode = InputMode::RotatingScene;
			break;
		}
	}
	if(action == GLFW_RELEASE && (button == GLFW_MOUSE_BUTTON_1 || button == GLFW_MOUSE_BUTTON_2)) {
		self->m_mode = InputMode::None;
	}

	if(oldMode != self->m_mode) {
		if(self->m_mode == InputMode::None) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
		else {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			glfwGetCursorPos(window, &self->m_prevCursorX, &self->m_prevCursorY);
		}
	}
}
	
void Application::mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	self->m_viewSettings.distance += ZoomSpeed * float(-yoffset);
}
	
void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	Application* self = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

	if(action == GLFW_PRESS) {
		SceneSettings::Light* light = nullptr;
		
		switch(key) {
		case GLFW_KEY_F1:
			light = &self->m_sceneSettings.lights[0];
			break;
		case GLFW_KEY_F2:
			light = &self->m_sceneSettings.lights[1];
			break;
		case GLFW_KEY_F3:
			light = &self->m_sceneSettings.lights[2];
			break;
		}

		if(light) {
			light->enabled = !light->enabled;
		}
	}
}
