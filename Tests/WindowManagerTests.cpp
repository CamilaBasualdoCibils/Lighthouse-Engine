#include <Lighthouse/Engine/WindowManager.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

TEST(WindowManagerTests, HandlesAreGenerationalAcrossDestroyAndReuse) {
    Engine::WindowManager windows;
    Engine::WindowHandle first;
    Engine::WindowHandle second;
    try {
        first = windows.CreateWindow({.title = "First", .width = 320, .height = 240, .visible = false});
        second = windows.CreateWindow({.title = "Second", .width = 320, .height = 240, .visible = false});
    } catch (const std::runtime_error&) {
        GTEST_SKIP() << "Native display is not available";
    }

    ASSERT_NE(windows.GetWindow(first), nullptr);
    ASSERT_NE(windows.GetWindow(second), nullptr);
    EXPECT_NE(first, second);
    windows.DestroyWindow(first);
    EXPECT_EQ(windows.GetWindow(first), nullptr);

    const auto replacement = windows.CreateWindow({.title = "Replacement", .width = 320, .height = 240, .visible = false});
    EXPECT_EQ(replacement.index, first.index);
    EXPECT_NE(replacement.generation, first.generation);
    EXPECT_NE(windows.GetWindow(replacement), nullptr);
}

TEST(WindowManagerTests, SupportsZeroWindowsAndInvalidHandles) {
    Engine::WindowManager windows;
    EXPECT_EQ(windows.GetWindow({}), nullptr);
    EXPECT_NO_THROW(windows.PollEvents());
}
