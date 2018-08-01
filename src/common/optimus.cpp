/*
 * Physically Based Rendering
 * Copyright (c) 2017-2018 Michał Siejak
 */

#if _WIN32
#define EXPORT_SYMBOL __declspec(dllexport)
#else
#define EXPORT_SYMBOL
#endif

// Enable usage of more performant GPUs on laptops.
extern "C" {
	EXPORT_SYMBOL int NvOptimusEnablement = 1;
	EXPORT_SYMBOL int AmdPowerXpressRequestHighPerformance = 1;
}
