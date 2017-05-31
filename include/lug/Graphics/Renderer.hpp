#pragma once

#include <cstdint>
#include <set>
#include <string>

#include <lug/Core/Version.hpp>
#include <lug/Graphics/Module.hpp>
#include <lug/Graphics/Render/Window.hpp>
#include <lug/System/Time.hpp>

namespace lug {
namespace Graphics {

class Graphics;

class LUG_GRAPHICS_API Renderer {
public:
    enum class Type : uint8_t {
        Vulkan
    };

    struct InitInfo {
        std::string shadersRoot;
    };

public:
    Renderer(Graphics& graphics);

    Renderer(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;

    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    virtual ~Renderer() = default;

    virtual bool beginInit(const std::string& appName, const Core::Version& appVersion, const InitInfo& initInfo) = 0;
    virtual bool finishInit() = 0;

    virtual bool beginFrame(const lug::System::Time &elapsedTime) = 0;
    virtual bool endFrame() = 0;

    virtual Render::Window* createWindow(Render::Window::InitInfo& initInfo) = 0;
    virtual Render::Window* getWindow() = 0;

    const InitInfo& getInfo() const;

protected:
    Graphics& _graphics;
    InitInfo _initInfo;
};

#include <lug/Graphics/Renderer.inl>

} // Graphics
} // lug
