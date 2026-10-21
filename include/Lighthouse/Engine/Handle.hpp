#pragma once

#include <cstdint>
#include <limits>
#include <memory>

namespace Engine {

template <typename Tag>
struct Handle {
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index{InvalidIndex};
    std::uint32_t generation{};

    constexpr bool IsValid() const noexcept { return index != InvalidIndex; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    friend constexpr bool operator==(Handle, Handle) = default;
};

struct WindowHandleTag;
struct RenderViewHandleTag;
struct RenderSessionHandleTag;
struct RenderTechniqueHandleTag;
struct OffscreenTargetHandleTag;

using WindowHandle = Handle<WindowHandleTag>;
using RenderViewHandle = Handle<RenderViewHandleTag>;
using RenderSessionHandle = Handle<RenderSessionHandleTag>;
using RenderTechniqueHandle = Handle<RenderTechniqueHandleTag>;
using OffscreenTargetHandle = Handle<OffscreenTargetHandleTag>;

class Scene;
class SceneHandle {
public:
    constexpr SceneHandle() noexcept = default;
    bool IsValid() const noexcept { return m_index != InvalidIndex && !m_lifetime.expired(); }
    explicit operator bool() const noexcept { return IsValid(); }
    std::uint32_t Index() const noexcept { return m_index; }
    std::uint32_t Generation() const noexcept { return m_generation; }
    friend bool operator==(const SceneHandle& a, const SceneHandle& b) noexcept {
        return a.m_index == b.m_index && a.m_generation == b.m_generation;
    }

private:
    friend class Scene;
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();
    SceneHandle(std::uint32_t index, std::uint32_t generation, const std::shared_ptr<void>& lifetime)
        : m_index(index), m_generation(generation), m_lifetime(lifetime) {}
    std::uint32_t m_index{InvalidIndex};
    std::uint32_t m_generation{};
    std::weak_ptr<void> m_lifetime;
};

} // namespace Engine
