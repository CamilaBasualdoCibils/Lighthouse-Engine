#include <Lighthouse/Engine/GraphicsBackend.hpp>

#include <algorithm>
#include <array>

namespace Engine {
namespace {
constexpr std::array Features{
    GraphicsFeature::ComputeShaders, GraphicsFeature::BindlessResources,
    GraphicsFeature::RayTracing, GraphicsFeature::MeshShaders, GraphicsFeature::AsyncCompute};
}

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

bool BackendRegistry::Register(GraphicsBackend backend, Factory factory) {
    if (!factory || IsRegistered(backend)) return false;
    m_entries.push_back({backend, std::move(factory)});
    return true;
}

bool BackendRegistry::Unregister(GraphicsBackend backend) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [=](const Entry& entry) { return entry.backend == backend; });
    if (it == m_entries.end()) return false;
    m_entries.erase(it);
    return true;
}

bool BackendRegistry::IsRegistered(GraphicsBackend backend) const {
    return std::any_of(m_entries.begin(), m_entries.end(), [=](const Entry& entry) { return entry.backend == backend; });
}

BackendSelectionResult BackendRegistry::Select(const GraphicsConfig& config) const {
    BackendSelectionResult result;
    if (config.backends.empty()) return result;
    const auto count = config.selection == BackendSelection::RequireFirst ? 1u : config.backends.size();
    struct Candidate { std::unique_ptr<IGraphicsBackend> backend; GraphicsBackend kind; std::size_t score; std::size_t order; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < count; ++i) {
        const auto kind = config.backends[i];
        BackendAttempt attempt{kind};
        const auto entry = std::find_if(m_entries.begin(), m_entries.end(), [=](const Entry& e) { return e.backend == kind; });
        if (entry == m_entries.end()) {
            attempt.diagnostic = "No implementation is registered for this backend";
            result.attempts.push_back(std::move(attempt));
            if (config.selection == BackendSelection::RequireFirst) break;
            continue;
        }
        attempt.registered = true;
        auto candidate = entry->factory();
        if (!candidate) {
            attempt.diagnostic = "Registered backend factory returned no instance";
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
        candidates.push_back({std::move(candidate), kind, score, i});
        if (config.selection == BackendSelection::FirstAvailable || config.selection == BackendSelection::RequireFirst) break;
    }
    if (candidates.empty()) return result;
    auto chosen = candidates.begin();
    if (config.selection == BackendSelection::BestAvailable) {
        chosen = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.score == b.score ? a.order > b.order : a.score < b.score;
        });
    }
    result.selectedBackend = chosen->kind;
    result.backend = std::move(chosen->backend);
    const auto attempt = std::find_if(result.attempts.begin(), result.attempts.end(), [&](const auto& a) { return a.backend == chosen->kind && a.initialized; });
    if (attempt != result.attempts.end()) { attempt->selected = true; attempt->diagnostic = "Selected: " + attempt->diagnostic; }
    for (auto& candidate : candidates) if (candidate.backend) candidate.backend->Shutdown();
    return result;
}

} // namespace Engine
