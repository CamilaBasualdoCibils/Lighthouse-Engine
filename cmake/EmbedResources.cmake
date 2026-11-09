include_guard(GLOBAL)
set_property(GLOBAL PROPERTY LIGHTHOUSE_RESOURCE_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/GenerateEmbeddedResources.cmake")
function(lighthouse_embed_resources target resource_namespace resource_header resource_root)
    file(GLOB_RECURSE resources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${resource_root}/*")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}.cpp")
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${target}.manifest")
    file(GENERATE OUTPUT "${manifest}" CONTENT "${resources}")
    get_property(generator GLOBAL PROPERTY LIGHTHOUSE_RESOURCE_GENERATOR)
    add_custom_command(OUTPUT "${generated}"
        COMMAND "${CMAKE_COMMAND}" "-DRESOURCE_ROOT=${resource_root}"
            "-DOUTPUT=${generated}" "-DRESOURCE_NAMESPACE=${resource_namespace}"
            "-DRESOURCE_HEADER=${resource_header}" -P "${generator}"
        DEPENDS ${resources} "${manifest}" "${generator}"
        COMMENT "Embedding ${target} resources" VERBATIM)
    add_library(${target} STATIC "${generated}")
    target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()
