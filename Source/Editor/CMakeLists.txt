cmake_minimum_required(VERSION 4.2.1)

project(Editor)

find_package(Vulkan REQUIRED)

include_directories(
  ${PROJECT_SOURCE_DIR} 
  ${Vulkan_INCLUDE_DIRS}
  ${VMA_INCLUDE_DIRS}
  ${GLM_INCLUDE_DIRS}
  ${STB_INCLUDE_DIRS}
  ${ASSIMP_INCLUDE_DIRS}
  ${CEREAL_INCLUDE_DIRS}
  ${TOML_PLUS_PLUS_INCLUDE_DIRS})

file(GLOB_RECURSE SRC_FILES
  ${PROJECT_SOURCE_DIR}/*.h
  ${PROJECT_SOURCE_DIR}/*.cpp
  ${GLM_INCLUDE_DIRS}/util/glm.natvis
  ${PROJECT_SOURCE_DIR}/spirv.natvis)

add_executable(${PROJECT_NAME} ${SRC_FILES})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
target_link_libraries(
  ${PROJECT_NAME}
    Core
    VkRHI
    ${Vulkan_LIBRARY}
    glfw
    Runtime
    assimp
    imgui)
add_dependencies(${PROJECT_NAME} BuildShaders)

