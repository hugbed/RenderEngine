glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\mvp.vert -o %~dp0\..\Build\Source\Samples\MainSample\Debug\mvp_vert.spv
glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\surface.frag -DLIT -o %~dp0\..\Build\Source\Samples\MainSample\Debug\surface_frag.spv
glslc.exe %~dp0\..\Source\Samples\MainSample\Shaders\surface.frag -o %~dp0\..\Build\Source\Samples\MainSample\Debug\surface_unlit_frag.spv
