cmake_minimum_required(VERSION 3.15...4.4)

project(test VERSION 1.0
        DESCRIPTION "Create window with wgpu"
        LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(test hello_world.cpp)

target_link_libraries(test PUBLIC dawn::webgpu_dawn glfw3webgpu)

if (WIN32)
add_custom_command(TARGET test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/utility/d3dcompiler_47.dll"
        "$<TARGET_FILE_DIR:hello_window>"
)
endif()
