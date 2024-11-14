#include "app.hpp"

#include <chrono>

#include <SDL3/SDL_events.h>

bool get_best_adapter(
    ComPtr<IDXGIFactory4> dxgi_factory4, ComPtr<IDXGIAdapter4> &out_dxgi_adapter4
);
bool has_tearing_support(ComPtr<IDXGIFactory4> dxgi_factory4);

[[nodiscard]] bool App::init()
{
#if defined(_DEBUG)
    // ------------
    // Enable debug/validation output
    // -------
    {
        ComPtr<ID3D12Debug> debug_interface;
        DXERR(
            D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)),
            "App::init: failed to get debug interface"
        );
        debug_interface->EnableDebugLayer();
        spdlog::trace("App::init: enabled d3d12 debug layer");
    }
#endif

    // ------------
    // Create DXGI Factory
    // -------
    ComPtr<IDXGIFactory4> dxgi_factory4;
    {
        UINT create_factory_flags = 0;
#if defined(_DEBUG)
        create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
        DXERR(
            CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&dxgi_factory4)),
            "App::init: failed to create dxgi factory 4"
        );
        spdlog::trace("App::init: created dxgi factory 4");
    }

    // ------------
    // Find best video adapter
    // -------
    ComPtr<IDXGIAdapter4> dxgi_adapter4;
    if (!get_best_adapter(dxgi_factory4, dxgi_adapter4))
    {
        spdlog::error("App::init: failed to find suitable adapter");
        return false;
    }
    spdlog::trace("App::init: found suitable adapter");

    // ------------
    // Create device
    // -------
    DXERR(
        D3D12CreateDevice(dxgi_adapter4.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
        "App::init: failed to create device"
    );
    spdlog::trace("App::init: created device");

#if defined(_DEBUG)
    // ------------
    // Filter debug messages
    // -------
    {
        ComPtr<ID3D12InfoQueue> info_queue;
        if (SUCCEEDED(m_device.As(&info_queue)))
        {
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            std::array severities{D3D12_MESSAGE_SEVERITY_INFO};
            std::array ids{
                // warns about unoptimized clear color
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                // triggered when using visual studio graphics debugging tools
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            };

            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumSeverities = static_cast<UINT>(severities.size());
            filter.DenyList.pSeverityList = severities.data();
            filter.DenyList.NumIDs = static_cast<UINT>(ids.size());
            filter.DenyList.pIDList = ids.data();

            DXERR(
                info_queue->PushStorageFilter(&filter),
                "App::init: failed to set storage filter"
            );
        }
        spdlog::trace("App::init: created device");
    }
#endif

    // ------------
    // Create command queue
    // -------
    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 0;
        DXERR(
            m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_command_queue)),
            "App::init: failed create command queue"
        );
        spdlog::trace("App::init: created command queue");
    }

    // ------------
    // Check for tearing support
    // -------
    m_allow_tearing = has_tearing_support(dxgi_factory4);
    spdlog::debug("App::init: allow tearing = {}", m_allow_tearing);

    // ------------
    // Create swapchain
    // -------
    {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        ));
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc{
            .Width = WINDOW_WIDTH,
            .Height = WINDOW_HEIGHT,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .Stereo = FALSE,
            // must be {1, 0} for flip model
            .SampleDesc = {1, 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = NUM_FRAMES,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
            .Flags = m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u,
        };
        ComPtr<IDXGISwapChain1> swapchain1;
        DXERR(
            dxgi_factory4->CreateSwapChainForHwnd(
                m_command_queue.Get(),
                hwnd,
                &swapchain_desc,
                nullptr,
                nullptr,
                &swapchain1
            ),
            "App::init: failed to create swapchain1"
        );
        DXERR(
            dxgi_factory4->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
            "App::init: failed to disable Alt+Enter"
        );
        DXERR(swapchain1.As(&m_swapchain), "App::init: failed to convert swapchain1 to swapchain4");
        spdlog::trace("App::init: created swapchain");

        m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();
    }

    // ------------
    // Create RTV descriptor heap
    // -------
    if (!create_descriptor_heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_FRAMES, m_rtv_descriptor_heap))
    {
        spdlog::error("App::init: failed to create rtv descriptor heap");
        return false;
    }
    spdlog::trace("App::init: created rtv descriptor heap");

    // ------------
    // Create RTVs
    // -------
    {
        m_rtv_descriptor_size =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
            m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart()
        );

        for (int i = 0; i < NUM_FRAMES; ++i)
        {
            DXERR(
                m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i])),
                "App::init: failed to get swapchain buffer"
            );
            m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, rtv_handle);
            rtv_handle.Offset(m_rtv_descriptor_size);
        }
        spdlog::trace("App::init: created rtvs");
    }

    // ------------
    // Create command allocators
    // -------
    for (int i = 0; i < NUM_FRAMES; ++i)
    {
        DXERR(
            m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_command_allocators[i])
            ),
            "App::init: failed to create command allocators"
        );
    }
    spdlog::trace("App::init: created command allocators");

    // ------------
    // Create command list
    // -------
    DXERR(
        m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_command_allocators[m_current_backbuffer_index].Get(),
            nullptr,
            IID_PPV_ARGS(&m_command_list)
        ),
        "App::init: failed to create command list"
    );
    DXERR(m_command_list->Close(), "App::init: failed to close command list");

    // ------------
    // Create fence
    // -------
    DXERR(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
        "App::init: failed to create fence"
    );
    m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fence_event)
    {
        spdlog::error("App::init: failed to create fence event");
        return false;
    }
    spdlog::trace("App::init: created fence and fence event");

    return true;
}

void App::run()
{
    spdlog::trace("App::run: entering loop");
    while (true)
    {
        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                break;
            }
        }

        if (!render_frame())
        {
            spdlog::error("App::run: render_frame failed");
            break;
        }
    }
    spdlog::trace("App::run: exited loop");

    spdlog::trace("App::run: flushing...");
    if (!flush())
    {
        spdlog::error("App::run: flush failed");
    }
}

bool App::render_frame()
{
    m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();
    DXERR(
        wait_for_fence_value(m_frame_fence_values[m_current_backbuffer_index]),
        "App::render_frame: failed to wait for fence"
    );

    ComPtr<ID3D12CommandAllocator> cmd_allocator = m_command_allocators[m_current_backbuffer_index];
    ComPtr<ID3D12Resource> backbuffer = m_backbuffers[m_current_backbuffer_index];

    DXERR(cmd_allocator->Reset(), "App::render_frame: failed to reset command allocator");
    DXERR(
        m_command_list->Reset(cmd_allocator.Get(), nullptr),
        "App::render_frame: failed to reset command list"
    );

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backbuffer.Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        m_command_list->ResourceBarrier(1, &barrier);
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
            m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
            m_current_backbuffer_index,
            m_rtv_descriptor_size
        );
        std::array clear_color{1.0f, 0.5f, 0.1f};
        m_command_list->ClearRenderTargetView(rtv_handle, clear_color.data(), 0, nullptr);
    }

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backbuffer.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        m_command_list->ResourceBarrier(1, &barrier);
    }

    {
        DXERR(m_command_list->Close(), "App::render_frame: failed to close command list");
        std::array<ID3D12CommandList *const, 1> lists{m_command_list.Get()};
        m_command_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    }

    {
        UINT flags = m_allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
        m_swapchain->Present(0, flags);
        DXERR(
            signal_fence(m_frame_fence_values[m_current_backbuffer_index]),
            "App::render_frame: failed to signal fence"
        );
    }

    return true;
}

bool App::create_descriptor_heap(
    D3D12_DESCRIPTOR_HEAP_TYPE type, UINT num_descriptors, ComPtr<ID3D12DescriptorHeap> &out_heap
)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = num_descriptors;
    heap_desc.Type = type;
    DXERR(
        m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&out_heap)),
        "App::create_descriptor_heap: failed to create descriptor heap"
    );

    return true;
}

bool App::signal_fence(uint64_t &out_value)
{
    out_value = ++m_fence_value;
    DXERR(
        m_command_queue->Signal(m_fence.Get(), out_value),
        "App::signal_fence: failed to signal fence"
    );
    return true;
}

bool App::wait_for_fence_value(uint64_t value)
{
    if (m_fence->GetCompletedValue() < value)
    {
        DXERR(
            m_fence->SetEventOnCompletion(value, m_fence_event),
            "App::wait_for_fence_value: failed to set event"
        );
        std::chrono::milliseconds duration = std::chrono::milliseconds::max();
        WaitForSingleObject(m_fence_event, static_cast<DWORD>(duration.count()));
    }
    return true;
}

bool App::flush()
{
    uint64_t wait_value;
    DXERR(signal_fence(wait_value), "App::flush: failed to signal fence");
    DXERR(wait_for_fence_value(wait_value), "App::flush: failed to wait for fence");
    return true;
}

bool get_best_adapter(ComPtr<IDXGIFactory4> dxgi_factory4, ComPtr<IDXGIAdapter4> &out_dxgi_adapter4)
{
    ComPtr<IDXGIAdapter1> dxgi_adapter1;

    SIZE_T max_dedicated_video_memory = 0;
    for (UINT i = 0; dxgi_factory4->EnumAdapters1(i, &dxgi_adapter1) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 dxgi_adapter_desc1;
        dxgi_adapter1->GetDesc1(&dxgi_adapter_desc1);

        if ((dxgi_adapter_desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            SUCCEEDED(D3D12CreateDevice(
                dxgi_adapter1.Get(),
                D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device),
                nullptr
            )) &&
            dxgi_adapter_desc1.DedicatedVideoMemory > max_dedicated_video_memory)
        {
            max_dedicated_video_memory = dxgi_adapter_desc1.DedicatedVideoMemory;
            DXERR(
                dxgi_adapter1.As(&out_dxgi_adapter4),
                "get_best_adapter: failed to cast dxgi_adapter1 to out_dxgi_adapter4"
            );
        }
    }

    return true;
}

bool has_tearing_support(ComPtr<IDXGIFactory4> dxgi_factory4)
{
    ComPtr<IDXGIFactory5> dxgi_factory5;
    if (SUCCEEDED(dxgi_factory4.As(&dxgi_factory5)))
    {
        BOOL allow_tearing = FALSE;
        if (FAILED(dxgi_factory5->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allow_tearing,
                sizeof(allow_tearing)
            )))
        {
            return false;
        }

        return allow_tearing == TRUE;
    }

    return false;
}
