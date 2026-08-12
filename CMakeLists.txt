cmake_minimum_required(VERSION 3.20)
project(Moba LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(moba
  src/main.cpp
  src/vulkan_context.cpp
  src/vulkan_context.hpp
  src/game.cpp
  src/game.hpp
  src/shader.cpp
  src/shader.hpp
)

target_include_directories(moba PRIVATE src /usr/include)

find_package(Vulkan REQUIRED)
target_link_libraries(moba PRIVATE Vulkan::Vulkan)

if(NOT DEFINED GLFW_INCLUDE_DIR)
  find_library(GLFW_LIBRARY NAMES glfw glfw3 REQUIRED)
  find_path(GLFW_INCLUDE_DIR NAMES GLFW/glfw3.h REQUIRED)
endif()

target_include_directories(moba PRIVATE ${GLFW_INCLUDE_DIR})
target_link_libraries(moba PRIVATE ${GLFW_LIBRARY})

# glslc from Vulkan SDK builds the shaders at build time into the source
# shaders/ dir so the binary finds them regardless of working directory.
add_custom_command(
  OUTPUT ${CMAKE_SOURCE_DIR}/shaders/instance.vert.spv ${CMAKE_SOURCE_DIR}/shaders/instance.frag.spv
  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/instance.vert -o ${CMAKE_SOURCE_DIR}/shaders/instance.vert.spv
  COMMAND glslc ${CMAKE_SOURCE_DIR}/shaders/instance.frag -o ${CMAKE_SOURCE_DIR}/shaders/instance.frag.spv
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/instance.vert ${CMAKE_SOURCE_DIR}/shaders/instance.frag
)

add_custom_target(moba_shaders ALL
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/instance.vert.spv ${CMAKE_SOURCE_DIR}/shaders/instance.frag.spv
)

add_dependencies(moba moba_shaders)
