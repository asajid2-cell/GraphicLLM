#include "DX12Device.h"
#include <spdlog/spdlog.h>
#include <dxgidebug.h>
#include <Windows.h>

namespace Cortex::Graphics {

Result<void> DX12Device::Initialize(const DeviceConfig& config) {
    spdlog::info("Initializing DX12 Device...");

    // Enable debug layer if requested; if it fails, fall back to release device.
    // When the debug layer is active we also enable DRED so that device-removed
    // hangs surface rich breadcrumbs and page-fault information.
    bool debugLayerEnabled = false;
    if (config.enableDebugLayer) {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            spdlog::info("D3D12 Debug Layer enabled");

            ComPtr<ID3D12Debug1> debugController1;
            if (SUCCEEDED(debugController.As(&debugController1))) {
                // Explicitly set GPU-based validation to the requested state so we can
                // force it OFF even if the environment/registry would enable it.
                debugController1->SetEnableGPUBasedValidation(
                    config.enableGPUValidation ? TRUE : FALSE);
                debugController1->SetEnableSynchronizedCommandQueueValidation(FALSE);
                if (config.enableGPUValidation) {
                    spdlog::info("GPU-based validation enabled");
                } else {
                    spdlog::info("GPU-based validation explicitly disabled");
                }
            }
            debugLayerEnabled = true;
        } else {
            spdlog::warn("Failed to enable D3D12 Debug Layer, continuing without it");
        }
    }

    m_debugLayerEnabled = debugLayerEnabled;

    if (debugLayerEnabled) {
        EnableDRED();
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
    UnregisterInfoQueueCallback();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
    m_dedicatedVideoMemoryBytes = 0;

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
            m_dedicatedVideoMemoryBytes = static_cast<std::uint64_t>(desc.DedicatedVideoMemory);

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

    // Optional: configure info queue. During GPU debugging we explicitly
    // break on corruption and error severities so the debugger stops at the
    // first offending call and surfaces a callstack. Warnings remain
    // non-breaking to avoid overly chatty behavior.
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
        // Only break when a debugger is attached; otherwise a "break on
        // corruption" turns into a hard crash on shutdown for end users.
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,
                                      IsDebuggerPresent() ? TRUE : FALSE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

        // Filter out noisy info messages
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
    }

    RegisterInfoQueueCallback();

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

Cortex::Result<DX12Device::VideoMemoryInfo> DX12Device::QueryVideoMemoryInfo() const {
    VideoMemoryInfo out{};

    if (!m_adapter) {
        return Cortex::Result<VideoMemoryInfo>::Err("DX12Device::QueryVideoMemoryInfo: adapter is null");
    }

    ComPtr<IDXGIAdapter3> adapter3;
    HRESULT hr = m_adapter.As(&adapter3);
    if (FAILED(hr) || !adapter3) {
        return Cortex::Result<VideoMemoryInfo>::Err("DX12Device::QueryVideoMemoryInfo: IDXGIAdapter3 not available");
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    hr = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr)) {
        return Cortex::Result<VideoMemoryInfo>::Err("DX12Device::QueryVideoMemoryInfo: QueryVideoMemoryInfo failed");
    }

    out.currentUsageBytes = static_cast<std::uint64_t>(info.CurrentUsage);
    out.budgetBytes = static_cast<std::uint64_t>(info.Budget);
    out.availableForReservationBytes = static_cast<std::uint64_t>(info.AvailableForReservation);

    return Cortex::Result<VideoMemoryInfo>::Ok(out);
}

void DX12Device::EnableDRED() {
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        spdlog::info("DX12 DRED (auto-breadcrumbs + page fault reporting) enabled");
    } else {
        spdlog::warn("DX12 DRED settings interface not available; device-removed diagnostics limited");
    }
}

void DX12Device::RegisterInfoQueueCallback() {
    if (!m_device || !m_debugLayerEnabled || m_infoQueueCallbackRegistered) {
        return;
    }

    ComPtr<ID3D12InfoQueue1> infoQueue1;
    if (FAILED(m_device.As(&infoQueue1)) || !infoQueue1) {
        return;
    }

    const HRESULT hr = infoQueue1->RegisterMessageCallback(
        &DX12Device::InfoQueueCallback,
        D3D12_MESSAGE_CALLBACK_FLAG_NONE,
        this,
        &m_infoQueueCallbackCookie);
    if (SUCCEEDED(hr)) {
        m_infoQueueCallbackRegistered = true;
        spdlog::info("DX12 InfoQueue callback registered");
    }
}

void DX12Device::UnregisterInfoQueueCallback() {
    if (!m_device || !m_infoQueueCallbackRegistered) {
        return;
    }

    ComPtr<ID3D12InfoQueue1> infoQueue1;
    if (SUCCEEDED(m_device.As(&infoQueue1)) && infoQueue1) {
        infoQueue1->UnregisterMessageCallback(m_infoQueueCallbackCookie);
    }

    m_infoQueueCallbackRegistered = false;
    m_infoQueueCallbackCookie = 0;
}

void CALLBACK DX12Device::InfoQueueCallback(D3D12_MESSAGE_CATEGORY category,
                                           D3D12_MESSAGE_SEVERITY severity,
                                           D3D12_MESSAGE_ID id,
                                           LPCSTR pDescription,
                                           void* pContext) {
    (void)category;
    (void)pContext;

    if (!pDescription) {
        return;
    }

    // Keep the callback lightweight and only surface high-severity issues.
    if (severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || severity == D3D12_MESSAGE_SEVERITY_ERROR) {
        spdlog::error("D3D12 validation: id={} {}", static_cast<int>(id), pDescription);
    }
}

} // namespace Cortex::Graphics
