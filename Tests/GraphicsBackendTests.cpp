#include <Lighthouse/Engine/GraphicsBackend.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

class TestBackend final : public Engine::IGraphicsBackend {
public:
    explicit TestBackend(Engine::GraphicsCapabilities capabilities, bool succeeds = true)
        : capabilities(capabilities), succeeds(succeeds) {}
    bool Initialize(std::string& diagnostic) override {
        ++initializeCount;
        if (!succeeds) diagnostic = "mock initialization failure";
        return succeeds;
    }
    Engine::GraphicsCapabilities Capabilities() const override { return capabilities; }
    void Shutdown() noexcept override { ++shutdownCount; }

    Engine::GraphicsCapabilities capabilities;
    bool succeeds;
    int initializeCount{};
    int shutdownCount{};
};

TEST(GraphicsBackendTests, FallsBackInPriorityOrderAndReportsDiagnostics) {
    Engine::BackendRegistry registry;
    auto first = std::make_shared<TestBackend>(Engine::GraphicsCapabilities{}, false);
    auto second = std::make_shared<TestBackend>(Engine::GraphicsCapabilities{.computeShaders = true});
    ASSERT_TRUE(registry.Register(Engine::GraphicsBackend::Vulkan, [=] { return std::make_unique<TestBackend>(*first); }));
    ASSERT_TRUE(registry.Register(Engine::GraphicsBackend::OpenGL, [=] { return std::make_unique<TestBackend>(*second); }));

    Engine::GraphicsConfig config;
    config.backends = {Engine::GraphicsBackend::Vulkan, Engine::GraphicsBackend::OpenGL};
    config.requirements.Require(Engine::GraphicsFeature::ComputeShaders);
    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, Engine::GraphicsBackend::OpenGL);
    ASSERT_EQ(result.attempts.size(), 2u);
    EXPECT_FALSE(result.attempts[0].initialized);
    EXPECT_TRUE(result.attempts[1].selected);
    EXPECT_FALSE(registry.IsRegistered(Engine::GraphicsBackend::D3D12));
}

TEST(GraphicsBackendTests, RequireFirstDoesNotFallBack) {
    Engine::BackendRegistry registry;
    registry.Register(Engine::GraphicsBackend::OpenGL, [] {
        return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{});
    });
    Engine::GraphicsConfig config;
    config.backends = {Engine::GraphicsBackend::Vulkan, Engine::GraphicsBackend::OpenGL};
    config.selection = Engine::BackendSelection::RequireFirst;

    auto result = registry.Select(config);

    EXPECT_FALSE(result);
    ASSERT_EQ(result.attempts.size(), 1u);
    EXPECT_FALSE(result.attempts.front().registered);
}

TEST(GraphicsBackendTests, BestAvailableScoresPreferredCapabilities) {
    Engine::BackendRegistry registry;
    registry.Register(Engine::GraphicsBackend::Vulkan, [] {
        return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{.computeShaders = true});
    });
    registry.Register(Engine::GraphicsBackend::OpenGL, [] {
        return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{.computeShaders = true, .rayTracing = true});
    });
    Engine::GraphicsConfig config;
    config.backends = {Engine::GraphicsBackend::Vulkan, Engine::GraphicsBackend::OpenGL};
    config.selection = Engine::BackendSelection::BestAvailable;
    config.requirements.Require(Engine::GraphicsFeature::ComputeShaders).Prefer(Engine::GraphicsFeature::RayTracing);

    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, Engine::GraphicsBackend::OpenGL);
    EXPECT_EQ(result.attempts.size(), 2u);
}

TEST(GraphicsBackendTests, RejectsBackendsMissingRequiredCapabilities) {
    Engine::BackendRegistry registry;
    registry.Register(Engine::GraphicsBackend::Vulkan, [] {
        return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{});
    });
    registry.Register(Engine::GraphicsBackend::D3D12, [] {
        return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{.rayTracing = true});
    });
    Engine::GraphicsConfig config;
    config.backends = {Engine::GraphicsBackend::Vulkan, Engine::GraphicsBackend::D3D12};
    config.requirements.Require(Engine::GraphicsFeature::RayTracing);

    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, Engine::GraphicsBackend::D3D12);
    EXPECT_NE(result.attempts.front().diagnostic.find("required capabilities"), std::string::npos);
}

TEST(GraphicsBackendTests, UnregisterRemovesFactory) {
    Engine::BackendRegistry registry;
    registry.Register(Engine::GraphicsBackend::Vulkan, [] { return std::make_unique<TestBackend>(Engine::GraphicsCapabilities{}); });
    EXPECT_TRUE(registry.Unregister(Engine::GraphicsBackend::Vulkan));
    EXPECT_FALSE(registry.IsRegistered(Engine::GraphicsBackend::Vulkan));
}

} // namespace
