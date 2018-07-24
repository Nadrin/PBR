/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#include <cstdio>
#include <cstring>
#include <memory>

#include "application.hpp"

#include "../opengl.hpp"
#include "../vulkan.hpp"

#if defined(PLATFORM_WINDOWS)
#include "../d3d11.hpp"
#include "../d3d12.hpp"
#endif

int main(int argc, char* argv[])
{
	try {
		RendererInterface* renderer;

		if(argc < 2) {
#if defined(PLATFORM_WINDOWS)
			renderer = new D3D11::Renderer;
#else
			renderer = new OpenGL::Renderer;
#endif
		}
		else if(strcmp(argv[1], "-opengl") == 0) {
			renderer = new OpenGL::Renderer;
		}
		else if(strcmp(argv[1], "-vulkan") == 0) {
			renderer = new Vulkan::Renderer;
		}
#if defined(PLATFORM_WINDOWS)
		else if(strcmp(argv[1], "-d3d11") == 0) {
			renderer = new D3D11::Renderer;
		}
		else if(strcmp(argv[1], "-d3d12") == 0) {
			renderer = new D3D12::Renderer;
		}
#endif
		else {
#if defined(PLATFORM_WINDOWS)
			std::fprintf(stderr, "Usage: %s [-opengl|-vulkan|-d3d11|-d3d12]\n", argv[0]);
#else
			std::fprintf(stderr, "Usage: %s [-opengl|-vulkan]\n", argv[0]);
#endif
			return 1;
		}

		Application().run(std::unique_ptr<RendererInterface>{renderer});
	}
	catch(const std::exception& e) {
		std::fprintf(stderr, "Error: %s\n", e.what());
		return -1;
	}
}
