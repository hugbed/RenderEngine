cmake_minimum_required(VERSION 4.2.1)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)

project(RenderEngine)

set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_DISABLE_SOURCE_CHANGES  ON)
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(SEND_ERROR "In-source builds are not allowed.")
endif ()

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)

# Remove 'lib' prefix for shared libraries on Windows
if (WIN32)
  set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif ()

# CMake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# Output in a bin directory
set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/../../Binaries/Win64)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Support folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Dependencies 
add_subdirectory(ThirdParty)

# Targets
add_subdirectory(Source/VkCore)
add_subdirectory(Source/Engine)
add_subdirectory(Source/Editor)
add_subdirectory(Source/Samples/MainSample)

# Set default project
set(DEFAULT_PROJECT MainSample)
if (WIN32)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${DEFAULT_PROJECT})
endif ()

# Always build shaders,
# compile-shaders2.py will rebuild only when necessary.

file(GLOB_RECURSE SHADER_FILES
    ${PROJECT_SOURCE_DIR}/Shaders/*.vert
    ${PROJECT_SOURCE_DIR}/Shaders/*.frag
    ${PROJECT_SOURCE_DIR}/Shaders/*.glsl)

add_custom_target(
  BuildShaders ALL
  DEPENDS ${SHADER_FILES}
  COMMAND py -3
    ${CMAKE_SOURCE_DIR}/Scripts/compile-shaders.py
      ${CMAKE_SOURCE_DIR}/Assets/Shaders
      ${CMAKE_SOURCE_DIR}/Assets/Generated/Shaders)

# Group all CMake projects under CMakePredefinedTargets
set_target_properties(uninstall PROPERTIES FOLDER "CMakePredefinedTargets")
