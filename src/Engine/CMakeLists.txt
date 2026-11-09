find_package(glslang CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(boost_describe CONFIG REQUIRED)
find_package(boost_program_options CONFIG REQUIRED)
if(NOT LIGHTHOUSE_IMGUI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
endif()
find_package(glm CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

file(GLOB_RECURSE _engine_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(FILTER _engine_sources EXCLUDE REGEX "/ImGuiView[.]cpp$")
add_library(LighthouseEngine STATIC ${_engine_sources})

if(LIGHTHOUSE_IMGUI)
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
    target_sources(LighthouseEngine PRIVATE ImGuiView.cpp)
    target_link_libraries(LighthouseEngine PRIVATE unofficial::imgui-node-editor::imgui-node-editor)
    target_link_libraries(LighthouseEngine PUBLIC Lighthouse::EngineEmbedded imgui::imgui implot::implot)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_IMGUI=1)
endif()
if (LIGHTHOUSE_ENABLE_TRACY)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_ENABLE_TRACY=1)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(LighthouseEngine PUBLIC Lighthouse::EngineEmbedded Tracy::TracyClient)
endif()
add_library(Lighthouse::Engine ALIAS LighthouseEngine)

target_include_directories(LighthouseEngine PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)
target_include_directories(LighthouseEngine PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${TINYGLTF_INCLUDE_DIR})
target_link_libraries(LighthouseEngine PUBLIC Lighthouse::EngineEmbedded
glm::glm
fg::FrameGraph
Boost::program_options
Boost::describe
spdlog::spdlog
PRIVATE
nlohmann_json::nlohmann_json
glfw
glslang::glslang
glslang::SPIRV
glslang::glslang-default-resource-limits
slang::slang
$<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc> )
target_compile_features(LighthouseEngine PUBLIC cxx_std_23)


if(NOT LIGHTHOUSE_IMGUI)
    target_link_libraries(LighthouseEngine PRIVATE PkgConfig::WAYLAND_EGL)
endif()
