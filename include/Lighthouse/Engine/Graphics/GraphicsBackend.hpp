#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

namespace Lighthouse {

class Window;

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

class IGraphicsBackend;
using GraphicsBackendFactory = std::function<std::unique_ptr<IGraphicsBackend>()>;

struct ConfiguredGraphicsBackend {
    std::string name;
    GraphicsBackendFactory factory;
};

struct GraphicsConfig {
    std::vector<std::string> backends;
    BackendSelection selection{BackendSelection::FirstAvailable};
    GraphicsRequirements requirements;
    std::vector<ConfiguredGraphicsBackend> configuredBackends;

    template <typename BackendType>
    void AddBackend() {
        static_assert(std::is_base_of_v<IGraphicsBackend, BackendType>,
                      "Configured backend must implement IGraphicsBackend");
        constexpr std::string_view backendName = BackendType::BackendName;
        const std::string name{backendName};
        if (std::find(backends.begin(), backends.end(), name) == backends.end()) {
            backends.push_back(name);
        }
        const auto factory = [] { return std::make_unique<BackendType>(); };
        const auto existing = std::find_if(configuredBackends.begin(), configuredBackends.end(),
            [&name](const ConfiguredGraphicsBackend& entry) { return entry.name == name; });
        if (existing == configuredBackends.end()) configuredBackends.push_back({name, factory});
        else existing->factory = factory;
    }
};

class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;
    virtual std::string_view Name() const noexcept = 0;
    virtual bool Initialize(std::string& diagnostic) = 0;
    virtual GraphicsCapabilities Capabilities() const = 0;
    virtual std::unique_ptr<IRenderDevice> CreateDevice(const DeviceConfig& config);
    virtual void Shutdown() noexcept = 0;
};

struct BackendAttempt {
    std::string backend;
    bool registered{};
    bool initialized{};
    bool selected{};
    std::string diagnostic;
};

struct BackendSelectionResult {
    std::unique_ptr<IGraphicsBackend> backend;
    std::string selectedBackend;
    std::vector<BackendAttempt> attempts;
    explicit operator bool() const noexcept { return static_cast<bool>(backend); }
};

class BackendRegistry {
public:
    using Factory = GraphicsBackendFactory;

    bool Register(std::string backend, Factory factory);
    bool Unregister(std::string_view backend);
    bool IsRegistered(std::string_view backend) const;
    BackendSelectionResult Select(const GraphicsConfig& config) const;

private:
    struct Entry { std::string backend; Factory factory; };
    std::vector<Entry> m_entries;
};

} // namespace Lighthouse
