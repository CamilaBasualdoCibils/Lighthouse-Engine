import LighthouseMaterial;

struct SolidColorLit : IMaterialDefinition
{
    typealias Base = LitBase;

    float4 color;
    float roughness;

    __init()
    {
        color = float4(1.0, 0.2, 0.1, 1.0);
        roughness = 0.6;
    }

    override void vertexLit(inout VertexData vertex)
    {
        vertex.position.x += 0.125;
    }

    override Surface fragmentLit(FragmentData fragment)
    {
        Surface surface;
        surface.albedo = color.rgb;
        surface.roughness = roughness;
        surface.metallic = 0.0;
        return surface;
    }
}
