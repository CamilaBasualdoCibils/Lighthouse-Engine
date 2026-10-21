#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Lighthouse/Engine/RenderSession.hpp>
#include <Lighthouse/Engine/RenderTechnique.hpp>
#include <Lighthouse/Engine/RenderView.hpp>
#include <Lighthouse/Engine/WindowManager.hpp>

namespace Engine {

class Renderer {
public:
    explicit Renderer(WindowManager* windows = nullptr) : m_windows(windows) {}

    RenderViewHandle CreateView(const RenderViewDesc& desc = {});
    bool DestroyView(RenderViewHandle view);
    RenderView* GetView(RenderViewHandle view) noexcept;
    const RenderView* GetView(RenderViewHandle view) const noexcept;

    RenderSessionHandle CreateSession(const RenderSessionDesc& desc = {});
    bool DestroySession(RenderSessionHandle session);
    RenderSession* GetSession(RenderSessionHandle session) noexcept;
    const RenderSession* GetSession(RenderSessionHandle session) const noexcept;
    bool IsSessionValid(RenderSessionHandle session) const noexcept;

    RenderTechniqueHandle RegisterTechnique(std::unique_ptr<IRenderTechnique> technique);
    bool UnregisterTechnique(RenderTechniqueHandle technique);
    IRenderTechnique* GetTechnique(RenderTechniqueHandle technique) noexcept;
    const IRenderTechnique* GetTechnique(RenderTechniqueHandle technique) const noexcept;

private:
    template <typename T> struct Slot { std::uint32_t generation{1}; std::unique_ptr<T> value; };
    template <typename T, typename Tag> static Handle<Tag> Add(std::vector<Slot<T>>& slots, std::unique_ptr<T> value);
    template <typename T, typename Tag> static bool Remove(std::vector<Slot<T>>& slots, Handle<Tag> handle);
    template <typename T, typename Tag> static T* Find(std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept;
    template <typename T, typename Tag> static const T* Find(const std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept;

    WindowManager* m_windows{};
    std::vector<Slot<RenderView>> m_views;
    std::vector<Slot<RenderSession>> m_sessions;
    std::vector<Slot<IRenderTechnique>> m_techniques;
};

} // namespace Engine
