# Render Engine

Vulkan rendering engine.

## Features

- Vulkan rendering engine abstractions.
- Scene loading using asimp
- Phong lighting
- Bindless material system
- Directional light shadow mapping

# Prerequisites

- CMake 3.19.2
- Vulkan SDK 1.2.162.0
- glslc.exe in the PATH environment variable

## Setup

- Make sure to have Vulkan SDK installed.
- Update submodules (git submodule update --init).
- Run ./Scripts/generate.bat
- A solution named RenderEngine.sln should be have been generated in ./Build :). Have fun!


# Todo

* Create a light system (at least one for Phong lighting) that manages descriptors and such for lights
* Move skybox descriptor management into Skybox, if we need an unlit material system we'll create one
* Have a shadow system instead of separate shadow map instances
