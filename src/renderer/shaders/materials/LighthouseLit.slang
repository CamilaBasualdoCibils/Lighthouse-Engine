import LighthouseMaterial;

struct LitMaterial<T : IMaterialDefinition> : IMaterial
    where T.Base == LitBase
{
    T definition = T();

    void vertex(inout VertexData vertex)
    {
        definition.vertexLit(vertex);
    }

    Surface fragment(FragmentData fragment)
    {
        return definition.fragmentLit(fragment);
    }
}
