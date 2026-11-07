#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Backends/Input/GLFW/GLFWInputBackend.hpp>
#include <gtest/gtest.h>
#include <cstdlib>
using namespace Lighthouse;
namespace {
class TestBackend : public IInputBackend {
public:
    inline static int starts{}, stops{};
    inline static bool fail{};
    std::string_view Name() const noexcept override { return "test"; }
    bool Initialize(WindowManager&, std::string&) override { ++starts; return !fail; }
    void Shutdown() noexcept override { ++stops; }
    std::vector<InputDeviceInfo> Enumerate(InputDeviceType) override { return {}; }
    std::shared_ptr<IInputDevice> OpenDevice(InputDeviceID, WindowHandle) override { return {}; }
};
}
TEST(InputSystem, RegistrationRetrievalAndShutdown) {
    TestBackend::starts = TestBackend::stops = 0; TestBackend::fail = false;
    Config config;
    config.AddInputBackend<TestBackend>(); config.AddInputBackend<TestBackend>();
    EngineRuntime runtime;
    ASSERT_TRUE(runtime.Initialize(config));
    EXPECT_EQ(runtime.GetInputSystem().GetBackend<TestBackend>().Name(), "test");
    EXPECT_THROW(runtime.GetInputSystem().GetBackend<GLFWInputBackend>(), std::out_of_range);
    EXPECT_EQ(TestBackend::starts, 1);
    runtime.Shutdown(); runtime.Shutdown();
    EXPECT_EQ(TestBackend::stops, 1);
    EXPECT_THROW(runtime.GetInputSystem().GetBackend<TestBackend>(), std::out_of_range);
}
TEST(InputSystem, FailedInitializationCleansUp) {
    TestBackend::starts = TestBackend::stops = 0; TestBackend::fail = true;
    Config config; config.AddInputBackend<TestBackend>();
    EngineRuntime runtime;
    EXPECT_FALSE(runtime.Initialize(config));
    EXPECT_EQ(TestBackend::stops, 1);
    EXPECT_THROW(runtime.GetInputSystem().GetBackend<TestBackend>(), std::out_of_range);
    TestBackend::fail = false;
}
TEST(GLFWInput, OwnershipWindowAndLifetime) {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Native display required";
    WindowManager windows;
    GLFWInputBackend first, second;
    std::string diagnostic;
    if (!first.Initialize(windows, diagnostic)) GTEST_SKIP() << diagnostic;
    ASSERT_TRUE(second.Initialize(windows, diagnostic)) << diagnostic;
    const auto keyboards = first.EnumerateKeyboards();
    ASSERT_EQ(keyboards.size(), 1u);
    EXPECT_EQ(first.EnumerateKeyboards()[0].id, keyboards[0].id);
    EXPECT_THROW(second.OpenDevice(keyboards[0].id), std::invalid_argument);
    EXPECT_THROW(first.OpenDevice(keyboards[0].id), std::invalid_argument);
    auto window = windows.CreateWindow({.visible = false});
    auto keyboard = first.OpenDevice(keyboards[0].id, window);
    auto mouse = first.OpenDevice(first.EnumerateMice()[0].id, window);
    EXPECT_TRUE(keyboard->ReadState().connected);
    EXPECT_TRUE(mouse->ReadState().connected);
    windows.DestroyWindow(window);
    EXPECT_FALSE(keyboard->ReadState().connected);
    first.Shutdown();
    EXPECT_FALSE(mouse->ReadState().connected);
    ASSERT_TRUE(first.Initialize(windows, diagnostic));
    EXPECT_THROW(first.OpenDevice(keyboards[0].id), std::invalid_argument);
}
