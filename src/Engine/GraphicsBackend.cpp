#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <algorithm>
#include <array>

namespace Lighthouse {
namespace {
constexpr std::array Features{
    GraphicsFeature::ComputeShaders, GraphicsFeature::BindlessResources,
    GraphicsFeature::RayTracing, GraphicsFeature::MeshShaders, GraphicsFeature::AsyncCompute};
}

std::unique_ptr<IRenderDevice> IGraphicsBackend::CreateDevice(const DeviceConfig&) { return {}; }

bool GraphicsCapabilities::Supports(GraphicsFeature feature) const noexcept {
    switch (feature) {
    case GraphicsFeature::ComputeShaders: return computeShaders;
    case GraphicsFeature::BindlessResources: return bindlessResources;
    case GraphicsFeature::RayTracing: return rayTracing;
    case GraphicsFeature::MeshShaders: return meshShaders;
    case GraphicsFeature::AsyncCompute: return asyncCompute;
    }
    return false;
}

GraphicsRequirements& GraphicsRequirements::Require(GraphicsFeature feature) noexcept {
    required |= static_cast<std::uint32_t>(feature); return *this;
}
GraphicsRequirements& GraphicsRequirements::Prefer(GraphicsFeature feature) noexcept {
    preferred |= static_cast<std::uint32_t>(feature); return *this;
}
GraphicsRequirements& GraphicsRequirements::Allow(GraphicsFeature feature) noexcept {
    optional |= static_cast<std::uint32_t>(feature); return *this;
}

bool BackendRegistry::Register(std::string backend, Factory factory) {
    if (backend.empty() || !factory || IsRegistered(backend)) return false;
    m_entries.push_back({std::move(backend), std::move(factory)});
    return true;
}

bool BackendRegistry::Unregister(std::string_view backend) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [=](const Entry& entry) { return entry.backend == backend; });
    if (it == m_entries.end()) return false;
    m_entries.erase(it);
    return true;
}

bool BackendRegistry::IsRegistered(std::string_view backend) const {
    return std::any_of(m_entries.begin(), m_entries.end(), [=](const Entry& entry) { return entry.backend == backend; });
}

BackendSelectionResult BackendRegistry::Select(const GraphicsConfig& config) const {
    BackendSelectionResult result;
    if (config.backends.empty()) return result;
    const auto count = config.selection == BackendSelection::RequireFirst ? 1u : config.backends.size();
    struct Candidate { std::unique_ptr<IGraphicsBackend> backend; std::string name; std::size_t score; std::size_t order; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& name = config.backends[i];
        BackendAttempt attempt{name};
        const auto configured = std::find_if(config.configuredBackends.begin(), config.configuredBackends.end(),
            [&name](const ConfiguredGraphicsBackend& e) { return e.name == name; });
        const auto entry = std::find_if(m_entries.begin(), m_entries.end(), [&name](const Entry& e) { return e.backend == name; });
        const auto registered = configured != config.configuredBackends.end() || entry != m_entries.end();
        if (!registered) {
            attempt.diagnostic = "No implementation is registered for this backend";
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        attempt.registered = true;
        auto candidate = configured != config.configuredBackends.end()
            ? configured->factory()
            : entry->factory();
        if (!candidate) {
            attempt.diagnostic = "Registered backend factory returned no instance";
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        if (candidate->Name() != name) {
            attempt.diagnostic = "Backend factory returned an implementation with a mismatched name";
            candidate->Shutdown();
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        std::string diagnostic;
        if (!candidate->Initialize(diagnostic)) {
            attempt.diagnostic = diagnostic.empty() ? "Backend initialization failed" : std::move(diagnostic);
            candidate->Shutdown();
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        attempt.initialized = true;
        const auto caps = candidate->Capabilities();
        bool satisfies = true;
        std::size_t score = 0;
        for (const auto feature : Features) {
            const auto bit = static_cast<std::uint32_t>(feature);
            if ((config.requirements.required & bit) && !caps.Supports(feature)) satisfies = false;
            if ((config.requirements.preferred & bit) && caps.Supports(feature)) score += 2;
            if ((config.requirements.optional & bit) && caps.Supports(feature)) ++score;
        }
        if (!satisfies) {
            attempt.diagnostic = "Backend does not satisfy all required capabilities";
            candidate->Shutdown();
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        attempt.diagnostic = "Backend initialized and satisfies requirements";
        result.attempts.push_back(std::move(attempt));
        candidates.push_back({std::move(candidate), name, score, i});
        if (config.selection == BackendSelection::FirstAvailable || config.selection == BackendSelection::RequireFirst) break;
    }
    if (candidates.empty()) return result;
    auto chosen = candidates.begin();
    if (config.selection == BackendSelection::BestAvailable) {
        chosen = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.score == b.score ? a.order > b.order : a.score < b.score;
        });
    }
    result.selectedBackend = chosen->name;
    result.backend = std::move(chosen->backend);
    const auto attempt = std::find_if(result.attempts.begin(), result.attempts.end(), [&](const auto& a) { return a.backend == chosen->name && a.initialized; });
    if (attempt != result.attempts.end()) { attempt->selected = true; attempt->diagnostic = "Selected: " + attempt->diagnostic; }
    for (auto& candidate : candidates) if (candidate.backend) candidate.backend->Shutdown();
    return result;
}

} // namespace Lighthouse
