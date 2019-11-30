cmake_minimum_required(VERSION 3.16)

project(Engine)

find_package(Vulkan REQUIRED)

include_directories(
  $(PROJECT_SOURCE_DIR) 
  ${Vulkan_INCLUDE_DIRS}
  ${GLM_INCLUDE_DIRS}
)

file(GLOB SRC_FILES
    ${PROJECT_SOURCE_DIR}/*.h
    ${PROJECT_SOURCE_DIR}/*.cpp) 

add_library(${PROJECT_NAME} ${SRC_FILES})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARY} VkCore)
