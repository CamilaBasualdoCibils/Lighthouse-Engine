#pragma once
struct ImGuiContext;
namespace Lighthouse::Detail
{
// Raise newly shown detached viewports once; preserve normal stacking after.
void EnableImGuiGlfwViewportRaiseOnShow();
void RouteImGuiGlfwCallbacks();
void RemoveImGuiGlfwCallbacks(ImGuiContext*);
} // namespace Lighthouse::Detail
