cmake_minimum_required(VERSION 3.8)
project(PBR)

set(PROJECT_DATA_DIR ${PROJECT_SOURCE_DIR}/../../data)

# For compatibility with MSVC
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Building in Release mode by default")
    set(CMAKE_BUILD_TYPE "Release")
endif()

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

# OpenGL renderer
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

# Vulkan renderer
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

install(TARGETS PBR DESTINATION ${PROJECT_DATA_DIR})

# Define a macro to compile a GLSL shader to SPIR-V
macro(add_spirv shadername stage)
    add_custom_command(
        OUTPUT ${PROJECT_BINARY_DIR}/spirv/${shadername}.spv
        COMMAND ${glslangValidator} -V -S ${stage} -o ${PROJECT_BINARY_DIR}/spirv/${shadername}.spv ${PROJECT_DATA_DIR}/shaders/glsl/${shadername}.glsl > /dev/null
        DEPENDS ${PROJECT_DATA_DIR}/shaders/glsl/${shadername}.glsl
    )
    set(spirvDependencies ${spirvDependencies} ${PROJECT_BINARY_DIR}/spirv/${shadername}.spv)
endmacro()

# If Vulkan has been found compile GLSL shaders
if(Vulkan_FOUND)
    file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/spirv)
    find_program(glslangValidator NAMES glslangValidator HINTS /opt/glslang/bin /opt/glslangValidator/bin)
    if(glslangValidator)
        message(STATUS "Found glslangValidator: ${glslangValidator}")

        add_spirv(equirect2cube_cs comp)
        add_spirv(irmap_cs comp)
        add_spirv(pbr_fs frag)
        add_spirv(pbr_vs vert)
        add_spirv(skybox_fs frag)
        add_spirv(skybox_vs vert)
        add_spirv(spbrdf_cs comp)
        add_spirv(spmap_cs comp)
        add_spirv(tonemap_fs frag)
        add_spirv(tonemap_vs vert)

        add_custom_target(spirv DEPENDS ${spirvDependencies})
        add_dependencies(PBR spirv)
        install(DIRECTORY ${PROJECT_BINARY_DIR}/spirv DESTINATION ${PROJECT_DATA_DIR}/shaders)
    else()
        message(SEND_ERROR "glslangValidator not found - will not compile GLSL shaders to SPIR-V during build (https://github.com/KhronosGroup/glslang/releases/tag/master-tot)")
    endif()
endif()
