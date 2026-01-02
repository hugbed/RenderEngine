* [X] Add the concept of project with directories for content
* [X] add files to folders (especially in Engine, create a Render folder)
* [X] remove main sample it's useless, move the grid to the engine, maybe some logic to the editor and/or make this a minimal sample program.
* [X] Add the concept of a scene with a transform graph (and remove the concept of a "model")
* [ ] Figure out how to update resources whose handles are index (textures, materials, etc.)
* [X] Move GPU rendering stuff into a Render lib or something (rendering specific SceneTree, etc.) -> Moved to a folder
* [X] Add the concept of render passes. Right now shadow maps are rendered in main.cpp
* [X] Rename Engine -> Runtime
* [X] Invert binary order, should be "Win64/Debug/bin" and not "Win64/bin/Debug"
* [ ] Add a base include over files in libs.
* [ ] Put stuff in namespaces
* [ ] add a macro(s) for asserts
* [ ] add custom logger & macros for errors/warnings
* [ ] clang-format
* [ ] Get rid of render passes
* [ ] Don't save shader shader hash if compilation failed
* [ ] Shader that don't exist anymore remain in the generated folder
* [ ] Shader build is successful in visual studio even if it fails?
* [ ] imgui can't find pdbs anymore now that binaries are somewhere else
* [ ] support adding debug info to shaders to debug with RenderDoc