import LighthouseMaterial;

struct SolidColorUnlit : IMaterialDefinition
{
    typealias Base = UnlitBase;

    float4 color;

    __init()
    {
        color = float4(0.1, 0.4, 1.0, 1.0);
    }

    override void vertexUnlit(inout VertexData vertex)
    {
        vertex.position.y += 0.25;
    }

    override Surface fragmentUnlit(FragmentData fragment)
    {
        Surface surface;
        surface.albedo = color.rgb;
        surface.roughness = 1.0;
        surface.metallic = 0.0;
        return surface;
    }
}
