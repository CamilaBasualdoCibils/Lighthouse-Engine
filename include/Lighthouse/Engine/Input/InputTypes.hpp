#pragma once
#include <cstddef>
namespace Lighthouse {
// USB HID keyboard usages, independent of the window/input backend.
enum class Key : std::size_t {
    A=4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit1=30, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Digit0,
    Enter=40, Escape, Backspace, Tab, Space, Minus, Equal, LeftBracket, RightBracket,
    Backslash, NonUSHash, Semicolon, Apostrophe, GraveAccent, Comma, Period, Slash, CapsLock,
    F1=58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    PrintScreen=70, ScrollLock, Pause, Insert, Home, PageUp, Delete, End, PageDown,
    Right, Left, Down, Up, NumLock, KeypadDivide, KeypadMultiply, KeypadSubtract,
    KeypadAdd, KeypadEnter, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6,
    Keypad7, Keypad8, Keypad9, Keypad0, KeypadDecimal,
    Menu=101, KeypadEqual=103, F13=104, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    LeftControl=224, LeftShift, LeftAlt, LeftSuper, RightControl, RightShift, RightAlt, RightSuper
};
enum class MouseButton : std::size_t { Left, Right, Middle, Button4, Button5, Button6, Button7, Button8 };
// Standard mapped gamepad order. Unmapped devices retain backend-specific indices.
enum class GamepadButton : std::size_t {
    A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb,
    DpadUp, DpadRight, DpadDown, DpadLeft
};
enum class GamepadAxis : std::size_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };
enum class MouseAxis : std::size_t { X, Y };
} // namespace Lighthouse
