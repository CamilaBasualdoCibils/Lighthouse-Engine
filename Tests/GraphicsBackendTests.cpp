#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Core/Config.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

class TestBackend final : public Lighthouse::IGraphicsBackend {
public:
    TestBackend(std::string name, Lighthouse::GraphicsCapabilities capabilities, bool succeeds = true)
        : name(std::move(name)), capabilities(capabilities), succeeds(succeeds) {}
    std::string_view Name() const noexcept override { return name; }
    bool Initialize(std::string& diagnostic) override {
        ++initializeCount;
        if (!succeeds) diagnostic = "mock initialization failure";
        return succeeds;
    }
    Lighthouse::GraphicsCapabilities Capabilities() const override { return capabilities; }
    void Shutdown() noexcept override { ++shutdownCount; }

    std::string name;
    Lighthouse::GraphicsCapabilities capabilities;
    bool succeeds;
    int initializeCount{};
    int shutdownCount{};
};

class ConfiguredTestBackend final : public Lighthouse::IGraphicsBackend {
public:
    static constexpr std::string_view BackendName = "ConfiguredTestBackend";
    std::string_view Name() const noexcept override { return BackendName; }
    bool Initialize(std::string&) override { return true; }
    Lighthouse::GraphicsCapabilities Capabilities() const override { return {}; }
    void Shutdown() noexcept override {}
};

TEST(GraphicsBackendTests, FallsBackInPriorityOrderAndReportsDiagnostics) {
    Lighthouse::BackendRegistry registry;
    registry.Register("Broken", [] { return std::make_unique<TestBackend>("Broken", Lighthouse::GraphicsCapabilities{}, false); });
    registry.Register("Compute", [] { return std::make_unique<TestBackend>("Compute", Lighthouse::GraphicsCapabilities{.computeShaders = true}); });

    Lighthouse::GraphicsConfig config;
    config.backends = {"Broken", "Compute"};
    config.requirements.Require(Lighthouse::GraphicsFeature::ComputeShaders);
    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, "Compute");
    ASSERT_EQ(result.attempts.size(), 2u);
    EXPECT_FALSE(result.attempts[0].initialized);
    EXPECT_TRUE(result.attempts[1].selected);
    EXPECT_FALSE(registry.IsRegistered("Missing"));
}

TEST(GraphicsBackendTests, RequireFirstDoesNotFallBack) {
    Lighthouse::BackendRegistry registry;
    registry.Register("Second", [] { return std::make_unique<TestBackend>("Second", Lighthouse::GraphicsCapabilities{}); });
    Lighthouse::GraphicsConfig config;
    config.backends = {"Missing", "Second"};
    config.selection = Lighthouse::BackendSelection::RequireFirst;

    auto result = registry.Select(config);

    EXPECT_FALSE(result);
    ASSERT_EQ(result.attempts.size(), 1u);
    EXPECT_FALSE(result.attempts.front().registered);
}

TEST(GraphicsBackendTests, BestAvailableScoresPreferredCapabilities) {
    Lighthouse::BackendRegistry registry;
    registry.Register("Basic", [] { return std::make_unique<TestBackend>("Basic", Lighthouse::GraphicsCapabilities{.computeShaders = true}); });
    registry.Register("RayTracing", [] { return std::make_unique<TestBackend>("RayTracing", Lighthouse::GraphicsCapabilities{.computeShaders = true, .rayTracing = true}); });
    Lighthouse::GraphicsConfig config;
    config.backends = {"Basic", "RayTracing"};
    config.selection = Lighthouse::BackendSelection::BestAvailable;
    config.requirements.Require(Lighthouse::GraphicsFeature::ComputeShaders).Prefer(Lighthouse::GraphicsFeature::RayTracing);

    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, "RayTracing");
    EXPECT_EQ(result.attempts.size(), 2u);
}

TEST(GraphicsBackendTests, RejectsBackendsMissingRequiredCapabilities) {
    Lighthouse::BackendRegistry registry;
    registry.Register("NoRayTracing", [] { return std::make_unique<TestBackend>("NoRayTracing", Lighthouse::GraphicsCapabilities{}); });
    registry.Register("RayTracing", [] { return std::make_unique<TestBackend>("RayTracing", Lighthouse::GraphicsCapabilities{.rayTracing = true}); });
    Lighthouse::GraphicsConfig config;
    config.backends = {"NoRayTracing", "RayTracing"};
    config.requirements.Require(Lighthouse::GraphicsFeature::RayTracing);

    auto result = registry.Select(config);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, "RayTracing");
    EXPECT_NE(result.attempts.front().diagnostic.find("required capabilities"), std::string::npos);
}

TEST(GraphicsBackendTests, UnregisterRemovesFactory) {
    Lighthouse::BackendRegistry registry;
    registry.Register("Named", [] { return std::make_unique<TestBackend>("Named", Lighthouse::GraphicsCapabilities{}); });
    EXPECT_TRUE(registry.Unregister("Named"));
    EXPECT_FALSE(registry.IsRegistered("Named"));
}

TEST(GraphicsBackendTests, ConfigCanRegisterBackendTypesDirectly) {
    Lighthouse::Config config;
    config.AddBackend<ConfiguredTestBackend>();
    Lighthouse::BackendRegistry registry;

    auto result = registry.Select(config.graphics);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.selectedBackend, "ConfiguredTestBackend");
    ASSERT_EQ(result.attempts.size(), 1u);
    EXPECT_TRUE(result.attempts.front().registered);
}

} // namespace
