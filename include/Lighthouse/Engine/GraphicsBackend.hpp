#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

enum class GraphicsBackend { Vulkan, D3D12, OpenGL };
enum class BackendSelection { RequireFirst, FirstAvailable, BestAvailable };

enum class GraphicsFeature : std::uint32_t {
    ComputeShaders = 1u << 0,
    BindlessResources = 1u << 1,
    RayTracing = 1u << 2,
    MeshShaders = 1u << 3,
    AsyncCompute = 1u << 4
};

struct GraphicsCapabilities {
    bool computeShaders{};
    bool bindlessResources{};
    bool rayTracing{};
    bool meshShaders{};
    bool asyncCompute{};

    bool Supports(GraphicsFeature feature) const noexcept;
};

struct GraphicsRequirements {
    std::uint32_t required{};
    std::uint32_t preferred{};
    std::uint32_t optional{};

    GraphicsRequirements& Require(GraphicsFeature feature) noexcept;
    GraphicsRequirements& Prefer(GraphicsFeature feature) noexcept;
    GraphicsRequirements& Allow(GraphicsFeature feature) noexcept;
};

struct GraphicsConfig {
    std::vector<GraphicsBackend> backends;
    BackendSelection selection{BackendSelection::FirstAvailable};
    GraphicsRequirements requirements;
};

class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;
    virtual bool Initialize(std::string& diagnostic) = 0;
    virtual GraphicsCapabilities Capabilities() const = 0;
    virtual void Shutdown() noexcept = 0;
};

struct BackendAttempt {
    GraphicsBackend backend;
    bool registered{};
    bool initialized{};
    bool selected{};
    std::string diagnostic;
};

struct BackendSelectionResult {
    std::unique_ptr<IGraphicsBackend> backend;
    GraphicsBackend selectedBackend{GraphicsBackend::Vulkan};
    std::vector<BackendAttempt> attempts;
    explicit operator bool() const noexcept { return static_cast<bool>(backend); }
};

class BackendRegistry {
public:
    using Factory = std::function<std::unique_ptr<IGraphicsBackend>()>;

    bool Register(GraphicsBackend backend, Factory factory);
    bool Unregister(GraphicsBackend backend);
    bool IsRegistered(GraphicsBackend backend) const;
    BackendSelectionResult Select(const GraphicsConfig& config) const;

private:
    struct Entry { GraphicsBackend backend; Factory factory; };
    std::vector<Entry> m_entries;
};

} // namespace Engine
