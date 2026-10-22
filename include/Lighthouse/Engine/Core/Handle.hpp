#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace Lighthouse {

template <typename Tag>
struct Handle {
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index{InvalidIndex};
    std::uint32_t generation{};
    std::uint64_t owner{};

    constexpr bool IsValid() const noexcept { return index != InvalidIndex; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    friend constexpr bool operator==(Handle, Handle) = default;
};

struct WindowHandleTag;
struct RenderViewHandleTag;
struct RenderTechniqueHandleTag;
struct SwapchainHandleTag;

using WindowHandle = Handle<WindowHandleTag>;
using RenderViewHandle = Handle<RenderViewHandleTag>;
using RenderTechniqueHandle = Handle<RenderTechniqueHandleTag>;
using SwapchainHandle = Handle<SwapchainHandleTag>;

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

} // namespace Lighthouse

namespace std {
template <typename Tag>
struct hash<Lighthouse::Handle<Tag>> {
    std::size_t operator()(const Lighthouse::Handle<Tag>& handle) const noexcept {
        std::size_t value = std::hash<std::uint32_t>{}(handle.index);
        value ^= std::hash<std::uint32_t>{}(handle.generation) + 0x9e3779b9u + (value << 6) + (value >> 2);
        value ^= std::hash<std::uint64_t>{}(handle.owner) + 0x9e3779b9u + (value << 6) + (value >> 2);
        return value;
    }
};
} // namespace std
