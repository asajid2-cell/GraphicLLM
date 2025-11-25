#pragma once

#include <SDL3/SDL.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <cstdint>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {
    class DX12Device;
    class DX12CommandQueue;
}

namespace Cortex {

struct WindowConfig {
    std::string title = "Cortex Engine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool fullscreen = false;
    bool vsync = true;
};

// Window wrapper with SDL3 and DX12 swapchain
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Result<void> Initialize(const WindowConfig& config, Graphics::DX12Device* device);

    // Complete swapchain initialization (must be called after command queue is created)
    Result<void> InitializeSwapChain(Graphics::DX12Device* device, Graphics::DX12CommandQueue* commandQueue);

    void Shutdown();

    // Frame management
    void Present();
    uint32_t GetCurrentBackBufferIndex() const;
    ID3D12Resource* GetCurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;
    HWND GetHWND() const { return m_hwnd; }

    // Window properties
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] float GetAspectRatio() const { return static_cast<float>(m_width) / static_cast<float>(m_height); }
    [[nodiscard]] SDL_Window* GetSDLWindow() const { return m_window; }
    [[nodiscard]] IDXGISwapChain3* GetSwapChain() const { return m_swapChain.Get(); }

    // Resize handling
    void OnResize(uint32_t width, uint32_t height);

private:
    Result<void> CreateSwapChain(Graphics::DX12Device* device, Graphics::DX12CommandQueue* commandQueue);
    Result<void> CreateRenderTargetViews(Graphics::DX12Device* device);
    void ReleaseRenderTargetViews();

    static constexpr uint32_t BUFFER_COUNT = 3;  // Triple buffering

    SDL_Window* m_window = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_vsync = true;
    HWND m_hwnd = nullptr;
    Graphics::DX12Device* m_device = nullptr;
    Graphics::DX12CommandQueue* m_commandQueue = nullptr;

    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Resource> m_backBuffers[BUFFER_COUNT];
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    uint32_t m_rtvDescriptorSize = 0;

    uint32_t m_currentBackBufferIndex = 0;
};

} // namespace Cortex
