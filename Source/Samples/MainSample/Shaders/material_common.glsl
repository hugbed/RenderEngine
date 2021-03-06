// --- Descriptor Sets Layout --- //

// --- Constants --- //
// fragment:
#define CONSTANT_NB_LIGHTS 0
#define CONSTANT_NB_SHADOW_MAPS 1
#define CONSTANT_NB_MATERIAL_SAMPLERS_2D 2
#define CONSTANT_NB_MATERIAL_SAMPLERS_CUBE 3
#define CONSTANT_NB_MATERIAL_PROPERTIES 4
// vertex:
#define CONSTANT_NB_MODELS 5

// Set 0 (View)
#define SET_VIEW 0
#define BINDING_VIEW_UNIFORMS 0
#define BINDING_VIEW_LIGHTS 1
#define BINDING_VIEW_SHADOW_MAPS 2
#define BINDING_VIEW_SHADOW_DATA 3

// Set 1 (Model)
#define SET_MODEL 1
#define BINDING_MODEL_UNIFORMS 0

// Set 2 (Material)
#define SET_MATERIAL 2
#define BINDING_MATERIAL_PROPERTIES 0
#define BINDING_MATERIAL_SAMPLERS_2D 1
#define BINDING_MATERIAL_SAMPLERS_CUBE 2
