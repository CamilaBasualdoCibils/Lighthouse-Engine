# Lighthouse Slang materials

Concrete materials import `LighthouseMaterial` and implement `IMaterialDefinition`.
The `Base` associated type must be `LitBase` or `UnlitBase`. Lit definitions override
`vertexLit` and `fragmentLit`; unlit definitions override `vertexUnlit` and
`fragmentUnlit`. The other pair has default implementations. Slang requires the
`override` modifier when replacing a default hook.

Slang requires an initializer in this contract so a selected material instance can be
constructed in the generated entry points. Define `__init()` when the material needs
default values for its fields. Slang's reflection API does not directly expose an
associated type binding, so Lighthouse asks the compiler to specialize the constrained
Lit and Unlit wrappers and selects the specialization that succeeds.
