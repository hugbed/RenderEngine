glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\primitive.vert -o %~dp0\..\Build\Source\Samples\MainSample\Debug\primitive_vert.spv

glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\surface.frag -o %~dp0\..\Build\Source\Samples\MainSample\Debug\surface_frag.spv
glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\surface_unlit.frag -o %~dp0\..\Build\Source\Samples\MainSample\Debug\surface_unlit_frag.spv

glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\skybox.vert -o %~dp0\..\Build\Source\Samples\MainSample\Debug\skybox_vert.spv
glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\skybox.frag -o %~dp0\..\Build\Source\Samples\MainSample\Debug\skybox_frag.spv

glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\grid.vert -o %~dp0\..\Build\Source\Samples\MainSample\Debug\grid_vert.spv
glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\grid.frag -o %~dp0\..\Build\Source\Samples\MainSample\Debug\grid_frag.spv
