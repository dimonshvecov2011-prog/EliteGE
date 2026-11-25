cmake_minimum_required(VERSION 3.20)
project(EliteGE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
    GIT_SHALLOW TRUE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw glm)

add_executable(EliteGE main.cpp)
target_link_libraries(EliteGE PRIVATE Vulkan::Vulkan glfw glm::glm)
target_compile_definitions(EliteGE PRIVATE GLFW_INCLUDE_NONE)

if(MSVC)
    target_compile_options(EliteGE PRIVATE /W4)
else()
    target_compile_options(EliteGE PRIVATE -Wall -Wextra -Wpedantic)
endif()
