cmake_minimum_required(VERSION 4.1.1)

project(ThirdParty)

# GSL
add_subdirectory(GSL)

# GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "GLFW_BUILD_EXAMPLES" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "GLFW_BUILD_TESTS" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "GLFW_BUILD_DOCS" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "GLFW_INSTALL" FORCE)
add_subdirectory(glfw)
set_target_properties(
  glfw
  update_mappings
    PROPERTIES FOLDER "ThirdParty/glfw")

set(GLFW_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/glfw/include PARENT_SCOPE)
set(GLM_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/glm PARENT_SCOPE)
set(GLM_ENABLE_CXX_20 PARENT_SCOPE)
set(STB_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/stb PARENT_SCOPE)
set(VMA_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/VulkanMemoryAllocator/include PARENT_SCOPE)

# SPIRV-Cross
set(SPIRV_CROSS_CLI OFF CACHE BOOL "SPIRV_CROSS_CLI" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "SPIRV_CROSS_ENABLE_TESTS" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "SPIRV_CROSS_ENABLE_HLSL" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "SPIRV_CROSS_ENABLE_MSL" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT ON CACHE BOOL "SPIRV_CROSS_ENABLE_REFLECT" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "SPIRV_CROSS_ENABLE_C_API" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "SPIRV_CROSS_ENABLE_UTIL" FORCE)
add_subdirectory(SPIRV-Cross)
set_target_properties(
  spirv-cross-core
  spirv-cross-cpp
  spirv-cross-glsl
  spirv-cross-reflect
    PROPERTIES FOLDER "ThirdParty/spriv-cross")

# IMGUI
find_package(Vulkan REQUIRED)
add_library(imgui STATIC)
target_include_directories(imgui PUBLIC ${Vulkan_INCLUDE_DIRS} imgui)
file(GLOB IMGUI_SRC_FILES
  ${PROJECT_SOURCE_DIR}/imgui/*.h
  ${PROJECT_SOURCE_DIR}/imgui/*.cpp
  ${PROJECT_SOURCE_DIR}/imgui/*.natvis
  ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.h
  ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
  ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.h
  ${PROJECT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.cpp)
target_sources(imgui PRIVATE ${IMGUI_SRC_FILES})
target_link_libraries(imgui PUBLIC ${Vulkan_LIBRARY} glfw)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# ASSIMP
set(BUILD_SHARED_LIBS OFF CACHE BOOL "BUILD_SHARED_LIBS" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "ASSIMP_BUILD_TESTS" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "ASSIMP_BUILD_ASSIMP_TOOLS" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/assimp)
set_target_properties(
  assimp
  UpdateAssimpLibsDebugSymbolsAndDLLs
  zlibstatic
    PROPERTIES FOLDER "ThirdParty/assimp")

set(ASSIMP_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/assimp/include PARENT_SCOPE)
set(ASSIMP_LIBRAIRIES assimp PARENT_SCOPE)

# tomlplusplus
set(TOML_PLUS_PLUS_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/tomlplusplus/include PARENT_SCOPE)
