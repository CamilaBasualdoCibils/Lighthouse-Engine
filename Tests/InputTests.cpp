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

#include <cmath>

namespace {
class ActionDevice : public IInputDevice {
public:
    InputDeviceInfo info;
    InputDeviceState state{true, std::vector<bool>(256), {0.f}};
    mutable int reads{};
    const InputDeviceInfo& Info() const noexcept override { return info; }
    InputDeviceState ReadState() const override { ++reads; return state; }
};
}
TEST(InputActions, MixedDevicesTransitionsAndSnapshot) {
    InputSystem input;
    auto set = input.CreateActionSet("Gameplay");
    auto action = set->AddAction("Attack", InputActionType::Button);
    auto first = std::make_shared<ActionDevice>(), second = std::make_shared<ActionDevice>();
    first->info.id = {1, 1}; second->info.id = {2, 1};
    action->Bind(first, InputControl::Button(MouseButton::Left));
    action->Bind(second, InputControl::Button(MouseButton::Left));
    auto other = set->AddAction("Other", InputActionType::Button);
    other->Bind(first, InputControl::Button(MouseButton::Left));
    int pressed = 0, released = 0;
    action->OnPressed([&] { ++pressed; EXPECT_TRUE(other->IsPressed()); });
    action->OnReleased([&] { ++released; });
    set->SetEnabled(true);
    first->state.buttons[0] = true;
    input.Update();
    EXPECT_EQ(first->reads, 1); EXPECT_EQ(pressed, 1);
    second->state.buttons[0] = true;
    input.Update();
    first->state.buttons[0] = false;
    input.Update();
    EXPECT_EQ(released, 0);
    second->state.connected = false;
    input.Update();
    EXPECT_EQ(released, 1);
    first->state.buttons[0] = true;
    input.Update();
    set->SetEnabled(false);
    input.Update();
    EXPECT_FALSE(action->IsPressed()); EXPECT_EQ(released, 2);
    EXPECT_THROW(action->Bind(nullptr, InputControl::Button(MouseButton::Left)), std::invalid_argument);
}
TEST(InputActions, CompositeAxesAndCallbackMutation) {
    InputSystem input;
    auto set = input.CreateActionSet("Gameplay");
    auto move = set->AddAction("Move", InputActionType::Axis2D);
    auto device = std::make_shared<ActionDevice>();
    move->BindComposite2D(device, Key::A, Key::D, Key::S, Key::W);
    auto axis = set->AddAction("Axis", InputActionType::Axis1D);
    axis->Bind(device, InputControl::Axis(GamepadAxis::LeftX));
    axis->Bind(device, InputControl::RawButton(999));
    int changes = 0;
    move->OnChanged([&](glm::vec2) { ++changes; input.RemoveActionSet(set); });
    set->SetEnabled(true);
    device->state.buttons[7] = device->state.buttons[26] = true;
    device->state.axes[0] = 0.75f;
    input.Update();
    EXPECT_NEAR(move->GetAxis2D().x, std::sqrt(0.5f), 0.0001f);
    EXPECT_NEAR(move->GetAxis2D().y, std::sqrt(0.5f), 0.0001f);
    EXPECT_FLOAT_EQ(axis->GetAxis1D(), 0.75f);
    EXPECT_EQ(changes, 1);
    input.Update(); EXPECT_EQ(device->reads, 1);
    auto opposite = input.CreateActionSet("Opposite");
    auto cancel = opposite->AddAction("Move", InputActionType::Axis2D);
    cancel->BindComposite2D(device, Key::A, Key::D, Key::S, Key::W);
    opposite->SetEnabled(true);
    device->state.buttons[4] = device->state.buttons[22] = true;
    input.Update();
    EXPECT_EQ(cancel->GetAxis2D(), glm::vec2(0));
}

TEST(InputActions, MouseDeltaBaselineDisableAndDisconnect) {
    InputSystem input;
    auto set = input.CreateActionSet("Look");
    auto look = set->AddAction("Look", InputActionType::Delta2D);
    auto mouse = std::make_shared<ActionDevice>();
    mouse->state.cursorX = 100; mouse->state.cursorY = 200;
    look->Bind(mouse, InputControl::Delta(MouseAxis::X), {1, 0});
    look->Bind(mouse, InputControl::Delta(MouseAxis::Y), {0, 1});
    set->SetEnabled(true);
    input.Update(); EXPECT_EQ(look->GetAxis2D(), glm::vec2(0));
    mouse->state.cursorX += 40; mouse->state.cursorY -= 20;
    input.Update(); EXPECT_EQ(look->GetAxis2D(), glm::vec2(40, -20));
    EXPECT_EQ(mouse->reads, 2);
    input.Update(); EXPECT_EQ(look->GetAxis2D(), glm::vec2(0));
    set->SetEnabled(false); input.Update();
    mouse->state.cursorX = 1000;
    set->SetEnabled(true); input.Update();
    EXPECT_EQ(look->GetAxis2D(), glm::vec2(0));
    mouse->state.connected = false; input.Update();
    mouse->state.connected = true; mouse->state.cursorX = -1000; input.Update();
    EXPECT_EQ(look->GetAxis2D(), glm::vec2(0));
    mouse->state.cursorX += 12; input.Update();
    EXPECT_EQ(look->GetAxis2D(), glm::vec2(12, 0));
}
TEST(InputTypes, NamedControlsUsePortableMappings) {
    EXPECT_EQ(InputControl::Button(Key::W).index, 26u);
    EXPECT_EQ(InputControl::Button(Key::Space).index, 44u);
    EXPECT_EQ(InputControl::Button(Key::LeftShift).index, 225u);
    EXPECT_EQ(InputControl::Button(MouseButton::Right).index, 1u);
    EXPECT_EQ(InputControl::Button(GamepadButton::DpadLeft).index, 14u);
    EXPECT_EQ(InputControl::Axis(GamepadAxis::RightY).index, 3u);
}
TEST(WindowCursor, ModesAndInvalidHandles) {
    WindowManager windows;
    EXPECT_THROW(windows.SetCursorMode({}, CursorMode::Captured), std::invalid_argument);
    WindowHandle handle;
    try { handle = windows.CreateWindow({.visible = false}); }
    catch (const std::runtime_error& error) { GTEST_SKIP() << error.what(); }
    EXPECT_NO_THROW(windows.SetCursorMode(handle, CursorMode::Captured));
    EXPECT_NO_THROW(windows.SetCursorMode(handle, CursorMode::Hidden));
    EXPECT_NO_THROW(windows.SetCursorMode(handle, CursorMode::Normal));
    windows.DestroyWindow(handle);
    EXPECT_THROW(windows.SetCursorMode(handle, CursorMode::Normal), std::invalid_argument);
}
