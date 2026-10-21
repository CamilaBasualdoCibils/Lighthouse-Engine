#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <Lighthouse/Engine/Scene/Entity.hpp>
#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>
#include <Lighthouse/Engine/Rendering/RenderTechnique.hpp>

namespace Lighthouse {

class WindowManager;
class IRenderDevice;
struct ViewBuildContext {
    RenderGraph& graph;
    IRenderDevice* device{};
};

struct ExplicitCamera {
    Vec3 position{};
    Vec3 direction{0.0f, 0.0f, 1.0f};
    float fieldOfViewDegrees{70.0f};
};
class CameraSource {
public:
    CameraSource() = default;
    CameraSource(const Entity& entity);
    CameraSource(ExplicitCamera camera) : m_source(camera) {}
    bool IsSet() const noexcept;
    bool IsValid() const noexcept;
    const std::variant<std::monostate, Entity, ExplicitCamera>& Value() const noexcept { return m_source; }
private:
    std::variant<std::monostate, Entity, ExplicitCamera> m_source;
};
struct RenderSettings {
    Vec3 clearColor{};
    bool clearColorEnabled{true};
    bool clearDepthEnabled{true};
};
using LayerMask = std::uint64_t;
struct RenderViewDesc {
    SceneHandle scene;
    CameraSource camera;
    RenderSettings settings;
    LayerMask visibleLayers{~LayerMask{0}};
};

class IRenderView {
public:
    explicit IRenderView(RenderViewDesc desc = {});
    virtual ~IRenderView() = default;
    IRenderView(const IRenderView&) = delete;
    IRenderView& operator=(const IRenderView&) = delete;
    void SetScene(SceneHandle value) noexcept { m_desc.scene = value; }
    void SetCamera(CameraSource value) { m_desc.camera = std::move(value); }
    void SetSettings(const RenderSettings& value) { m_desc.settings = value; }
    void SetVisibleLayers(LayerMask value) noexcept { m_desc.visibleLayers = value; }
    SceneHandle GetScene() const noexcept { return m_desc.scene; }
    const CameraSource& GetCamera() const noexcept { return m_desc.camera; }
    const RenderSettings& GetSettings() const noexcept { return m_desc.settings; }
    LayerMask GetVisibleLayers() const noexcept { return m_desc.visibleLayers; }
    bool HasValidReferences() const noexcept;
    virtual GraphImageHandle BuildGraph(ViewBuildContext& ctx);

    template <typename T, typename... Args> TechniqueRef<T> AddTechnique(Args&&... args) {
        static_assert(std::is_base_of_v<IRenderTechnique, T>);
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        std::uint32_t index = 0;
        while (index < m_techniques.size() && m_techniques[index].value) ++index;
        if (index == m_techniques.size()) m_techniques.emplace_back();
        auto& slot = m_techniques[index];
        slot.value = std::move(value);
        slot.dependencies.clear();
        return {{index, slot.generation, m_ownerId}};
    }
    template <typename T> T* GetTechnique(TechniqueRef<T> ref) noexcept {
        return dynamic_cast<T*>(GetTechnique(ref.handle));
    }
    template <typename T> const T* GetTechnique(TechniqueRef<T> ref) const noexcept {
        return dynamic_cast<const T*>(GetTechnique(ref.handle));
    }
    IRenderTechnique* GetTechnique(RenderTechniqueHandle handle) noexcept;
    const IRenderTechnique* GetTechnique(RenderTechniqueHandle handle) const noexcept;
    bool RemoveTechnique(RenderTechniqueHandle handle);
    template <typename T> bool RemoveTechnique(TechniqueRef<T> ref) { return RemoveTechnique(ref.handle); }
    template <typename T, typename... Dependencies>
    void SetDependencies(TechniqueRef<T> technique, Dependencies... dependencies) {
        static_assert((std::is_same_v<decltype(dependencies.handle), RenderTechniqueHandle> && ...));
        auto* slot = FindSlot(technique.handle);
        if (!slot) throw std::logic_error("Technique reference is invalid");
        slot->dependencies = {dependencies.handle...};
    }
    std::vector<RenderTechniqueHandle> TechniqueOrder() const;
    std::vector<RenderTechniqueHandle> Techniques() const;
    void SetFinalOutput(RenderTechniqueHandle technique) noexcept { m_finalTechnique = technique; }
    template <typename T> void SetFinalOutput(TechniqueRef<T> technique) noexcept { SetFinalOutput(technique.handle); }
    RenderTechniqueHandle FinalOutput() const noexcept { return m_finalTechnique; }
    std::uint64_t OwnerId() const noexcept { return m_ownerId; }
    void AddViewDependency(RenderViewHandle handle);
    const std::vector<RenderViewHandle>& ViewDependencies() const noexcept { return m_viewDependencies; }
    const std::vector<RenderTechniqueHandle>& TechniqueDependencies(RenderTechniqueHandle handle) const;

protected:
    GraphImageHandle BuildTechniques(ViewBuildContext& ctx, GraphImageHandle target = InvalidGraphImage);

private:
    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<IRenderTechnique> value;
        std::vector<RenderTechniqueHandle> dependencies;
    };
    Slot* FindSlot(RenderTechniqueHandle handle) noexcept;
    const Slot* FindSlot(RenderTechniqueHandle handle) const noexcept;
    RenderViewDesc m_desc;
    std::uint64_t m_ownerId;
    std::vector<Slot> m_techniques;
    RenderTechniqueHandle m_finalTechnique;
    std::vector<RenderViewHandle> m_viewDependencies;
};

class IOutputView : public IRenderView {
public:
    using IRenderView::IRenderView;
};

class WindowView final : public IOutputView {
public:
    WindowView(WindowHandle window, RenderViewDesc desc = {}, SwapchainConfig config = {})
        : IOutputView(std::move(desc)), window(window), presentation(config) {}
    WindowHandle window;
    SwapchainConfig presentation;
    SwapchainHandle swapchain;
    GraphImageHandle BuildGraph(ViewBuildContext& ctx) override;
private:
    friend class Renderer;
    WindowManager* m_windows{};
};
class ExportView final : public IOutputView {
public:
    ExportView(ImageHandle image, ImageViewHandle imageView, ImageDesc description, RenderViewDesc desc = {})
        : IOutputView(std::move(desc)), image(image), imageView(imageView), description(description) {}
    ImageHandle image;
    ImageViewHandle imageView;
    ImageDesc description;
    GraphImageHandle BuildGraph(ViewBuildContext& ctx) override;
};
class RenderTextureView final : public IRenderView {
public:
    using IRenderView::IRenderView;
    GraphImageHandle BuildGraph(ViewBuildContext& ctx) override;
};

} // namespace Lighthouse
