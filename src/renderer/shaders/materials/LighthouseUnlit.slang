import LighthouseMaterial;

struct UnlitMaterial<T : IMaterialDefinition> : IMaterial
    where T.Base == UnlitBase
{
    T definition = T();

    void vertex(inout VertexData vertex)
    {
        definition.vertexUnlit(vertex);
    }

    Surface fragment(FragmentData fragment)
    {
        return definition.fragmentUnlit(fragment);
    }
}
