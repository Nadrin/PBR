cmake_minimum_required(VERSION 3.8)
project(PBR)

find_package(PkgConfig REQUIRED)
find_package(OpenGL)
find_package(Vulkan)

pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(ASSIMP REQUIRED assimp)

set(srcCommon
    ../../src/common/application.cpp
    ../../src/common/application.hpp
    ../../src/common/image.cpp
    ../../src/common/image.hpp
    ../../src/common/main.cpp
    ../../src/common/mesh.cpp
    ../../src/common/mesh.hpp
    ../../src/common/renderer.hpp
    ../../src/common/utils.cpp
    ../../src/common/utils.hpp
)

set(srcLibraries
    ../../lib/stb/src/libstb.c
)

set(includePath
    ../../lib/glm/include
    ../../lib/stb/include
)

if(OpenGL_FOUND)
    set(srcRenderers ${srcRenderers}
        ../../src/opengl.cpp
        ../../src/opengl.hpp
    )
    set(srcLibraries ${srcLibraries}
        ../../lib/glad/src/glad.c
    )
    set(includePath ${includePath}
        ../../lib/glad/include
    )
    set(features ${features} ENABLE_OPENGL)
endif()

if(Vulkan_FOUND)
    set(srcRenderers ${srcRenderers}
        ../../src/vulkan.cpp
        ../../src/vulkan.hpp
    )
    set(srcLibraries ${srcLibraries}
        ../../lib/volk/src/volk.c
    )
    set(includePath ${includePath}
        ../../lib/volk/include
    )
    set(features ${features} ENABLE_VULKAN)
endif()

add_executable(PBR ${srcCommon} ${srcLibraries} ${srcRenderers})

target_compile_features(PBR PRIVATE cxx_std_14)
target_compile_definitions(PBR PRIVATE GLFW_INCLUDE_NONE GLM_ENABLE_EXPERIMENTAL ${features})
target_include_directories(PBR PRIVATE ${includePath} ${GLFW_INCLUDE_DIRS} ${ASSIMP_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS} ${VULKAN_INCLUDE_DIRS})
target_link_libraries(PBR dl ${GLFW_LIBRARIES} ${ASSIMP_LIBRARIES} ${OPENGL_LIBRARIES} ${VULKAN_LIBRARIES})
