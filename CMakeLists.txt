cmake_minimum_required(VERSION 3.14...4.4)
project(examples LANGUAGES C CXX)

# include(FetchContent)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

option(USE_WAYLAND "Build with Wayland support" ON)
option(USE_X11 "Build with X11 support" ON)

if (NOT WIN32)
    if (USE_WAYLAND)
    add_definitions(-D_GLFW_WAYLAND)
    endif()

    if (USE_X11)
        add_definitions(-D_GLFW_X11)
    endif()
endif()


# FetchContent_Declare(
#   glfw
#   GIT_REPOSITORY https://github.com/glfw/glfw.git
#   GIT_TAG        3.4
# )
# FetchContent_MakeAvailable(glfw)

find_package(Threads REQUIRED)
find_package(Dawn REQUIRED)

add_library(webgpu ALIAS dawn::webgpu_dawn)

add_subdirectory(vendors/glfw3webgpu)
add_subdirectory(vendors/glfw-3.5.1)

target_link_libraries(glfw3webgpu PUBLIC dawn::webgpu_dawn glfw)

option(BUILD_EXAMPLES "BUILD_EXAMPLES" ON)


if (BUILD_EXAMPLES) 
    add_subdirectory(examples)
endif()

