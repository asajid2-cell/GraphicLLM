#include "DX12Device.h"
#include <spdlog/spdlog.h>
#include <dxgidebug.h>

namespace Cortex::Graphics {

Result<void> DX12Device::Initialize(const DeviceConfig& config) {
    spdlog::info("Initializing DX12 Device...");

    // Enable debug layer if requested; if it fails, fall back to release device
    bool debugLayerEnabled = false;
    if (config.enableDebugLayer) {
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            spdlog::info("D3D12 Debug Layer enabled");

            if (config.enableGPUValidation) {
                ComPtr<ID3D12Debug1> debugController1;
                if (SUCCEEDED(debugController.As(&debugController1))) {
                    debugController1->SetEnableGPUBasedValidation(TRUE);
                    spdlog::info("GPU-based validation enabled");
                }
            }
            debugLayerEnabled = true;
        } else {
            spdlog::warn("Failed to enable D3D12 Debug Layer, continuing without it");
        }
#endif
    }

    // Create DXGI Factory
    auto factoryResult = CreateFactory();
    if (factoryResult.IsErr()) {
        return Result<void>::Err(factoryResult.Error());
    }

    // Select adapter (GPU)
    auto adapterResult = SelectAdapter();
    if (adapterResult.IsErr()) {
        return Result<void>::Err(adapterResult.Error());
    }

    // Create device
    auto deviceResult = CreateDevice(config.minFeatureLevel);
    if (deviceResult.IsErr()) {
        return Result<void>::Err(deviceResult.Error());
    }

    // Check for tearing support
    CheckTearingSupport();

    spdlog::info("DX12 Device initialized successfully");
    return Result<void>::Ok();
}

void DX12Device::Shutdown() {
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();

    spdlog::info("DX12 Device shut down");
}

Result<void> DX12Device::CreateFactory() {
    UINT dxgiFactoryFlags = 0; // disable DXGI debug factory to avoid runtime breaks

    HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create DXGI Factory");
    }

    return Result<void>::Ok();
}

Result<void> DX12Device::SelectAdapter() {
    ComPtr<IDXGIAdapter1> adapter;

    // Try to find a hardware adapter
    for (UINT adapterIndex = 0;
         SUCCEEDED(m_factory->EnumAdapterByGpuPreference(
             adapterIndex,
             DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&adapter)));
         ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Check if adapter supports D3D12
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
            m_adapter = adapter;

            // Log adapter info
            char adapterName[128];
            size_t converted;
            wcstombs_s(&converted, adapterName, sizeof(adapterName), desc.Description, _TRUNCATE);
            spdlog::info("Selected GPU: {}", adapterName);
            spdlog::info("  Dedicated Video Memory: {} MB", desc.DedicatedVideoMemory / (1024 * 1024));

            return Result<void>::Ok();
        }
    }

    return Result<void>::Err("No compatible GPU adapter found");
}

Result<void> DX12Device::CreateDevice(D3D_FEATURE_LEVEL minFeatureLevel) {
    HRESULT hr = D3D12CreateDevice(
        m_adapter.Get(),
        minFeatureLevel,
        IID_PPV_ARGS(&m_device)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create D3D12 device");
    }

    // Optional: configure info queue without breaking on messages
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
        // Do not break on errors to avoid abrupt termination
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

        // Filter out noisy info messages
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
    }
#endif

    return Result<void>::Ok();
}

void DX12Device::CheckTearingSupport() {
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;

    if (SUCCEEDED(m_factory.As(&factory5))) {
        HRESULT hr = factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing,
            sizeof(allowTearing)
        );

        m_supportsTearing = SUCCEEDED(hr) && allowTearing;

        if (m_supportsTearing) {
            spdlog::info("Variable refresh rate (tearing) supported");
        }
    }
}

} // namespace Cortex::Graphics
