#pragma once
struct ImGuiContext;
namespace Lighthouse::Detail
{
void RouteImGuiGlfwCallbacks();
void RemoveImGuiGlfwCallbacks(ImGuiContext*);
} // namespace Lighthouse::Detail
