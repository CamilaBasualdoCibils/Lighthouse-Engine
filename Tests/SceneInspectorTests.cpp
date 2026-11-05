#include <Lighthouse/Engine/Scene/Component.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>
#include <gtest/gtest.h>

#ifdef LIGHTHOUSE_IMGUI
namespace {
struct InspectedComponent {
    int calls{};
    static void Inspector(InspectedComponent& component) {
        ++component.calls;
        ImGui::Button("Shared label");
    }
};
struct OtherInspectedComponent {
    int calls{};
    static void Inspector(OtherInspectedComponent& component) {
        ++component.calls;
        ImGui::Button("Shared label");
    }
};
struct UninspectedComponent { int value{42}; };
LIGHTHOUSE_REGISTER_COMPONENT_INSPECTOR(InspectedComponent);
LIGHTHOUSE_REGISTER_COMPONENT_INSPECTOR(OtherInspectedComponent);

TEST(SceneInspectorTests, InvokesInspectorsOnStoredComponentsAndSkipsOthers) {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    Lighthouse::Scene scene("Inspector test");
    auto first = scene.CreateEntity("First");
    auto second = scene.CreateEntity("Second");
    auto empty = scene.CreateEntity("No components");
    auto& firstComponent = first.Add<InspectedComponent>();
    auto& secondComponent = second.Add<InspectedComponent>();
    auto& otherComponent = first.Add<OtherInspectedComponent>();
    auto& skipped = first.Add<UninspectedComponent>();
    RegisterComponentInspector<UninspectedComponent>();
    // Also cover a reflected type that has no Inspector function.
    entt::meta_factory<UninspectedComponent>();

    // Open every entity node without requiring a window system or renderer.
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("Scene inspector");
    for (auto entity : {first.ID(), second.ID(), empty.ID()}) {
        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        ImGui::GetStateStorage()->SetBool(ImGui::GetID("##Entity"), true);
        ImGui::PopID();
    }
    scene.DrawSceneInspector();
    ImGui::End();
    ImGui::Render();

    EXPECT_EQ(firstComponent.calls, 1);
    EXPECT_EQ(secondComponent.calls, 1);
    EXPECT_EQ(otherComponent.calls, 1);
    EXPECT_EQ(skipped.value, 42);
    ImGui::DestroyContext(context);
}
} // namespace
#endif
