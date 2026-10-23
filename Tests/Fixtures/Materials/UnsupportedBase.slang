import LighthouseMaterial;

struct UnsupportedMaterialBase {}

struct UnsupportedBase : IMaterialDefinition
{
    typealias Base = UnsupportedMaterialBase;

    __init() {}
    override void vertexLit(inout VertexData vertex) {}
    override Surface fragmentLit(FragmentData fragment)
    {
        Surface surface;
        surface.albedo = float3(1.0, 1.0, 1.0);
        surface.roughness = 1.0;
        surface.metallic = 0.0;
        return surface;
    }
}
