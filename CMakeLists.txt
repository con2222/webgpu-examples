cmake_minimum_required(VERSION 3.14...4.4)
project(examples LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

find_package(Threads REQUIRED)
find_package(Dawn REQUIRED)

add_library(webgpu ALIAS dawn::webgpu_dawn)

add_subdirectory(vendors/glfw3webgpu)
target_link_libraries(glfw3webgpu PUBLIC dawn::webgpu_dawn glfw)

option(BUILD_EXAMPLES "BUILD_EXAMPLES" ON)


if (BUILD_EXAMPLES) 
    add_subdirectory(examples)
endif()

