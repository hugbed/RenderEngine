#extension GL_EXT_nonuniform_qualifier : enable

#define Bindless 1

#define BindlessDescriptorSet 0

#define BindlessUniformBinding 0
#define BindlessStorageBinding 1
#define BindlessSamplerBinding 2

#define GetLayoutVariableName(Name) u##Name##Register

// todo (hbedard): temporarily define this here to avoid discrepancy with shader reflection
#define MAX_DESCRIPTOR_COUNT 1024

// Register uniform
#define RegisterUniform(Name, Struct) \
  layout(set = BindlessDescriptorSet, binding = BindlessUniformBinding) \
      uniform Name Struct \
      GetLayoutVariableName(Name)[MAX_DESCRIPTOR_COUNT]

// Register storage buffer
#define RegisterBuffer(Layout, BufferAccess, Name, Struct) \
  layout(Layout, set = BindlessDescriptorSet, \
         binding = BindlessStorageBinding) \
  BufferAccess buffer Name Struct GetLayoutVariableName(Name)[MAX_DESCRIPTOR_COUNT]

// Access a specific resource
#define GetResource(Name, Index) \
  GetLayoutVariableName(Name)[Index]

// Register empty resources
// to be compliant with the pipeline layout
// even if the shader does not use all the descriptors
RegisterUniform(DummyUniform, { uint ignore; });
RegisterBuffer(std430, readonly, DummyBuffer, { uint ignore; });

// Register textures
layout(set = BindlessDescriptorSet, binding = BindlessSamplerBinding) \
    uniform sampler2D uGlobalTextures2D[MAX_DESCRIPTOR_COUNT];
layout(set = BindlessDescriptorSet, binding = BindlessSamplerBinding) \
    uniform samplerCube uGlobalTexturesCube[MAX_DESCRIPTOR_COUNT];

#define GetTexture2D(index) uGlobalTextures2D[index]
#define GetTextureCube(index) uGlobalTexturesCube[index]
