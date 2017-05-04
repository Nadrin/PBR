/*
 * Physically Based Rendering
 * Copyright (c) 2017 Michał Siejak
 */

#include <cstdio>
#include <cstring>
#include <memory>

#include "common/application.hpp"
#include "opengl.hpp"

int main(int argc, char* argv[])
{
	try {
		RendererInterface* renderer;
		if(argc < 2 || strcmp(argv[1], "-opengl") == 0) {
			renderer = new OpenGL::Renderer;
		}
		else {
			std::fprintf(stderr, "Usage: %s [-opengl]\n", argv[0]);
			return 1;
		}

		Application().run(std::unique_ptr<RendererInterface>{renderer});
	}
	catch(const std::exception& e) {
		std::fprintf(stderr, "Error: %s\n", e.what());
		return -1;
	}
}
