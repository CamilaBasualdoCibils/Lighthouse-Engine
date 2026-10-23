struct VertexData
{
    float3 position;
    float3 normal;
    float2 uv;
}

struct FragmentData
{
    float3 position;
    float3 normal;
    float2 uv;
}

struct Surface
{
    float3 albedo;
    float roughness;
    float metallic;
}

interface IMaterial
{
    void vertex(inout VertexData vertex);
    Surface fragment(FragmentData fragment);
}

interface IMaterialDefinition
{
    associatedtype Base;
    __init();

    void vertexLit(inout VertexData vertex) {}
    Surface fragmentLit(FragmentData fragment)
    {
        Surface surface;
        surface.albedo = float3(1.0, 1.0, 1.0);
        surface.roughness = 1.0;
        surface.metallic = 0.0;
        return surface;
    }

    void vertexUnlit(inout VertexData vertex) {}
    Surface fragmentUnlit(FragmentData fragment)
    {
        Surface surface;
        surface.albedo = float3(1.0, 1.0, 1.0);
        surface.roughness = 1.0;
        surface.metallic = 0.0;
        return surface;
    }
}

struct LitBase {}
struct UnlitBase {}
