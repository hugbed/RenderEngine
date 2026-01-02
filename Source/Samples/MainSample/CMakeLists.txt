cmake_minimum_required(VERSION 4.1.1)

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
  ${PROJECT_SOURCE_DIR}/*.cpp)

add_executable(${PROJECT_NAME}
  ${SRC_FILES}
  ${GLM_INCLUDE_DIRS}/util/glm.natvis)
target_link_libraries(
  ${PROJECT_NAME}
    VkRHI
    Runtime)
add_dependencies(${PROJECT_NAME} BuildShaders)
source_group(TREE "${PROJECT_SOURCE_DIR}" FILES ${SRC_FILES})

if (MSVC)
  set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif ()
