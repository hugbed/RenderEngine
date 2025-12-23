cmake_minimum_required(VERSION 3.19)

project(Core)

find_package(Vulkan REQUIRED)

include_directories(${PROJECT_SOURCE_DIR})

file(GLOB SRC_FILES
    ${PROJECT_SOURCE_DIR}/*.h
    ${PROJECT_SOURCE_DIR}/*.cpp) 

add_library(${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR})

