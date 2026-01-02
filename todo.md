* [X] Add the concept of project with directories for content
* [ ] Add a base include over files in libs.
* [ ] Put stuff in namespaces
* [ ] It's allowed to have folders for similar files
* [ ] Rename GraphicsPipelineSystem to PipelineCache, TextureSystem to TextureCache
* [ ] add files to folders (especially in Engine, create a Render folder)
* [ ] add a macro(s) for asserts
* [ ] add custom logger for errors/warnings
* [ ] remove main sample it's useless, move the grid to the engine, maybe some logic to the editor and/or make this a minimal sample program.
* [ ] clang-format
* [ ] Get rid of render pass
* [ ] Get rid of descriptor sets and pools
* [ ] Add the concept of a scene with a transform graph (and remove the concept of a "model")
* [ ] ECS Sparse set or at least sparse set for persistent IDs (otherwise nothing can ever be removed!!)
      Nothing glues stuff together right now, it's just a bunch of randomly indexed arrays of structures
      Also everything is referred to by index! what if we need to sort stuff?
* [ ] Move GPU rendering stuff into a Render lib or something (rendering specific SceneTree, etc.)
* [ ] Add the concept of render passes. Right now shadow maps are rendered in main.cpp
* [X] Rename Engine -> Runtime
* [X] Invert binary order, should be "Win64/Debug/bin" and not "Win64/bin/Debug"
* [ ] Don't save shader shader hash if compilation failed
* [ ] Shader build is successful in visual studio even if it fails?