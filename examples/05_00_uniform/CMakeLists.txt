cmake_minimum_required(VERSION 3.15...4.4)

project(uniform VERSION 1.0
        DESCRIPTION "Uniform buffer"
        LANGUAGES C CXX
)

set(FILE_NAME uniform)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${FILE_NAME} uniform.cpp ${CMAKE_SOURCE_DIR}/common/context.cpp)
target_include_directories(${FILE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/vendors ${CMAKE_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})


target_link_libraries(${FILE_NAME} PUBLIC dawn::webgpu_dawn glfw3webgpu)

if (WIN32)
add_custom_command(TARGET ${FILE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/utility/d3dcompiler_47.dll"
        "$<TARGET_FILE_DIR:uniform>"
)
endif()
