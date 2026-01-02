cmake_minimum_required(VERSION 3.19)

project(VkRHI)

find_package(Vulkan REQUIRED)

include_directories(
  $(PROJECT_SOURCE_DIR) 
  ${Vulkan_INCLUDE_DIRS}
  ${VMA_INCLUDE_DIRS}
)

file(GLOB_RECURSE SRC_FILES
    ${PROJECT_SOURCE_DIR}/*.h
    ${PROJECT_SOURCE_DIR}/*.cpp
    ${PROJECT_SOURCE_DIR}/spirv.natvis) 

add_library(${PROJECT_NAME} ${SRC_FILES})
target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(
  ${PROJECT_NAME}
    Core
    ${Vulkan_LIBRARY}
    glfw
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-reflect
    Microsoft.GSL::GSL)
source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${SRC_FILES})
