cmake_minimum_required(VERSION 3.16)

project(MainSample)

find_package(Vulkan REQUIRED)

include_directories(
  ${PROJECT_SOURCE_DIR} 
  ${Vulkan_INCLUDE_DIRS}
  ${VMA_INCLUDE_DIRS}
  ${GLM_INCLUDE_DIRS}
  ${STB_INCLUDE_DIRS}
  ${ASSIMP_INCLUDE_DIRS})

file(GLOB_RECURSE SRC_FILES
    ${PROJECT_SOURCE_DIR}/*.h
    ${PROJECT_SOURCE_DIR}/*.cpp
    ${GLM_INCLUDE_DIRS}/util/glm.natvis)

add_executable(${PROJECT_NAME} ${SRC_FILES})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
target_link_libraries(
  ${PROJECT_NAME}
    ${Vulkan_LIBRARY}
    glfw
    VkCore
    Engine
    assimp)

if (WIN32)
  set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/Debug)
endif ()

add_custom_command(
  TARGET ${PROJECT_NAME} PRE_BUILD
  COMMAND py -3 ${CMAKE_SOURCE_DIR}/Scripts/compile-shaders.py $<CONFIG>)