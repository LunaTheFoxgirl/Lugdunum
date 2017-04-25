#include <chrono>
#include <algorithm>
#include <lug/Config.hpp>
#include <lug/Graphics/Light/Directional.hpp>
#include <lug/Graphics/Light/Point.hpp>
#include <lug/Graphics/Light/Spot.hpp>
#include <lug/Graphics/Render/Queue.hpp>
#include <lug/Graphics/Render/Gui.hpp>
#include <lug/Graphics/Scene/MeshInstance.hpp>
#include <lug/Graphics/Scene/ModelInstance.hpp>
#include <lug/Graphics/Scene/Node.hpp>
#include <lug/Graphics/Vulkan/Render/Camera.hpp>
#include <lug/Graphics/Vulkan/Render/Mesh.hpp>
#include <lug/Graphics/Vulkan/Render/Model.hpp>
#include <lug/Graphics/Vulkan/Render/View.hpp>
#include <lug/Graphics/Vulkan/Renderer.hpp>
#include <lug/Math/Matrix.hpp>
#include <lug/Math/Vector.hpp>
#include <lug/Math/Geometry/Transform.hpp>
#include <lug/System/Logger/Logger.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {

using MeshInstance = ::lug::Graphics::Scene::MeshInstance;

Gui::Gui() {}

bool Gui::beginFrame()
{
    return false;
}

bool Gui::render() {
    return true;
}

bool Gui::endFrame()
{
    return false;
}

void Gui::destroy() {
}

void Gui::init(float width, float height)
{
    // Color scheme
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    // Dimensions
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

void Gui::initRessources(VkRenderPass /*renderPass*/, VkQueue /*copyQueue*/) {

}

bool Gui::initDepthBuffers(const std::vector<std::unique_ptr<API::ImageView>>& /*imageViews*/) {
    return true;
}

bool Gui::initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& /*imageViews*/) {
    return true;
}

} // Render
} // Vulkan
} // Graphics
} // lug
