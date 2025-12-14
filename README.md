# Render Engine

Vulkan rendering engine.

## Features

- Vulkan rendering engine abstractions.
- Scene loading using asimp
- Phong lighting
- Bindless material system
- Directional light shadow mapping

# Prerequisites

- CMake >= 4.2.1
- C++17 compatible toolchain
- Vulkan SDK >= 1.4.328.1
- python 3.14.2
- glslc.exe >=  in the PATH environment variable
- clang-format?

## Setup

- Make sure to have Vulkan SDK installed.
- Update submodules (`git submodule update --init`).
- Run `./Scripts/generate.bat`
- A solution named RenderEngine.sln should be have been generated in `./Build`.
- Compile shaders with
```
py .\compile-shaders.py "PathTo\RenderEngine\Source\Samples\MainSample\Shaders" "PathTo\RenderEngine\Build\Source\Samples\MainSample"
```

# Todo

* Create a light system (at least one for Phong lighting) that manages descriptors and such for lights
* Move skybox descriptor management into Skybox, if we need an unlit material system we'll create one
* Have a shadow system instead of separate shadow map instances
