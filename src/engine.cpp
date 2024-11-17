#include "engine.hpp"

#include <d3dcompiler.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

bool get_best_adapter(
    ComPtr<IDXGIFactory4> dxgi_factory4, ComPtr<IDXGIAdapter4> &out_dxgi_adapter4
);

bool has_tearing_support(ComPtr<IDXGIFactory4> dxgi_factory4);

bool Engine::init(SDL_Window *window, uint64_t width, uint32_t height)
{
#if defined(_DEBUG)
    // ------------
    // Enable debug layer
    // -------
    {
        ComPtr<ID3D12Debug> debug_interface;
        DXERR(
            D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)),
            "Engine::init: failed to get debug interface"
        );
        debug_interface->EnableDebugLayer();
        spdlog::trace("Engine::init: enabled d3d12 debug layer");
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
            "Engine::init: failed to create dxgi factory 4"
        );
        spdlog::trace("Engine::init: created dxgi factory 4");
    }

    // ------------
    // Find best video adapter
    // -------
    ComPtr<IDXGIAdapter4> dxgi_adapter4;
    if (!get_best_adapter(dxgi_factory4, dxgi_adapter4))
    {
        spdlog::error("Engine::init: failed to find suitable adapter");
        return false;
    }
    spdlog::trace("Engine::init: found suitable adapter");

    // ------------
    // Create device
    // -------
    DXERR(
        D3D12CreateDevice(dxgi_adapter4.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)),
        "Engine::init: failed to create device"
    );
    spdlog::trace("Engine::init: created device");

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
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
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
                "Engine::init: failed to set storage filter"
            );
        }
        spdlog::trace("Engine::init: created device");
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
            "Engine::init: failed create command queue"
        );
        spdlog::trace("Engine::init: created command queue");
    }

    // ------------
    // Check for tearing support
    // -------
    m_allow_tearing = has_tearing_support(dxgi_factory4);
    spdlog::debug("Engine::init: allow tearing = {}", m_allow_tearing);

    // ------------
    // Create swapchain
    // -------
    m_swapchain_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc{
        .Width = static_cast<UINT>(width),
        .Height = static_cast<UINT>(height),
        .Format = m_swapchain_format,
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
    {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
            nullptr
        ));

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
            "Engine::init: failed to create swapchain1"
        );
        DXERR(
            dxgi_factory4->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
            "Engine::init: failed to disable Alt+Enter"
        );
        DXERR(
            swapchain1.As(&m_swapchain),
            "Engine::init: failed to convert swapchain1 to swapchain4"
        );
        spdlog::trace("Engine::init: created swapchain");

        m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();
    }

    // ------------
    // Create RTV descriptor heap
    // -------
    if (!create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            NUM_FRAMES,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_rtv_heap
        ))
    {
        spdlog::error("Engine::init: failed to create rtv descriptor heap");
        return false;
    }
    spdlog::trace("Engine::init: created rtv descriptor heap");

    // ------------
    // Create RTVs
    // -------
    if (!update_render_target_views())
    {
        spdlog::error("Engine::init: failed to create rtvs");
        return false;
    }
    spdlog::trace("Engine::init: created rtvs");

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
            "Engine::init: failed to create command allocators"
        );
    }
    spdlog::trace("Engine::init: created command allocators");

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
        "Engine::init: failed to create command list"
    );
    DXERR(m_command_list->Close(), "Engine::init: failed to close command list");

    // ------------
    // Create fence
    // -------
    DXERR(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
        "Engine::init: failed to create fence"
    );
    m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fence_event)
    {
        spdlog::error("Engine::init: failed to create fence event");
        return false;
    }
    spdlog::trace("Engine::init: created fence and fence event");

    // ------------
    // Create objects for immediate submit
    // -------
    {
        DXERR(
            m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_immediate_submit.command_allocator)
            ),
            "Engine::init: failed to create command allocator for immediate submit"
        );
        m_immediate_submit.command_allocator->SetName(L"immediate submit command allocator");

        DXERR(
            m_device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_immediate_submit.command_allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&m_immediate_submit.command_list)
            ),
            "Engine::init: failed to create command list for immediate submit"
        );
        m_immediate_submit.command_list->SetName(L"immediate submit command list");
        DXERR(
            m_immediate_submit.command_list->Close(),
            "Engine::init: failed to close command list for immediate submit"
        );

        DXERR(
            m_device
                ->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_immediate_submit.fence)),
            "Engine::init: failed to create fence for immediate submit"
        );
        m_immediate_submit.fence->SetName(L"immediate submit command fence");
        m_immediate_submit.fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_immediate_submit.fence_event)
        {
            spdlog::error("Engine::init: failed to create fence event for immediate submit");
            return false;
        }

        spdlog::trace("Engine::init: created immediate submit objects");
    }

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

bool Engine::resize(uint32_t new_width, int32_t new_height)
{
    for (int i = 0; i < NUM_FRAMES; ++i)
    {
        m_backbuffers[i].Reset();
        m_frame_fence_values[i] = m_frame_fence_values[m_current_backbuffer_index];
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    DXERR(
        m_swapchain->GetDesc(&swap_chain_desc),
        "Engine::handle_resize: failed to get previous swapchain description"
    );
    DXERR(
        m_swapchain->ResizeBuffers(
            NUM_FRAMES,
            new_width,
            new_height,
            swap_chain_desc.BufferDesc.Format,
            swap_chain_desc.Flags
        ),
        "Engine::handle_resize: failed to resize buffers"
    );
    m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();

    if (!update_render_target_views())
    {
        spdlog::error("Engine::handle_resize: failed to update rtvs");
        return false;
    }

    return true;
}

bool Engine::render_frame(
    std::function<void(ID3D12GraphicsCommandList *, ID3D12Resource *, D3D12_CPU_DESCRIPTOR_HANDLE)>
        &&render_func
)
{
    m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();
    DXERR(
        wait_for_fence_value(
            m_fence.Get(),
            m_fence_event,
            m_frame_fence_values[m_current_backbuffer_index]
        ),
        "Engine::render_frame: failed to wait for fence"
    );

    ComPtr<ID3D12CommandAllocator> cmd_allocator = m_command_allocators[m_current_backbuffer_index];
    ComPtr<ID3D12Resource> backbuffer = m_backbuffers[m_current_backbuffer_index];

    DXERR(cmd_allocator->Reset(), "Engine::render_frame: failed to reset command allocator");
    DXERR(
        m_command_list->Reset(cmd_allocator.Get(), nullptr),
        "Engine::render_frame: failed to reset command list"
    );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
        m_current_backbuffer_index,
        m_rtv_descriptor_size
    );
    render_func(m_command_list.Get(), backbuffer.Get(), rtv_handle);

    DXERR(m_command_list->Close(), "Engine::render_frame: failed to close command list");
    std::array<ID3D12CommandList *const, 1> lists{m_command_list.Get()};
    m_command_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    UINT present_flags = m_allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    m_swapchain->Present(0, present_flags);
    DXERR(
        signal_fence(
            m_fence.Get(),
            m_fence_value,
            m_frame_fence_values[m_current_backbuffer_index]
        ),
        "Engine::render_frame: failed to signal fence"
    );

    return true;
}

bool Engine::create_descriptor_heap(
    D3D12_DESCRIPTOR_HEAP_TYPE type, UINT num_descriptors, D3D12_DESCRIPTOR_HEAP_FLAGS flags,
    ComPtr<ID3D12DescriptorHeap> &out_heap
)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = num_descriptors;
    heap_desc.Type = type;
    heap_desc.Flags = flags;
    DXERR(
        m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&out_heap)),
        "Engine::create_descriptor_heap: failed to create descriptor heap"
    );

    return true;
}

bool Engine::update_render_target_views()
{
    m_rtv_descriptor_size =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(m_rtv_heap->GetCPUDescriptorHandleForHeapStart());

    for (int i = 0; i < NUM_FRAMES; ++i)
    {
        DXERR(
            m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backbuffers[i])),
            "Engine::update_render_target_views: failed to get swapchain buffer"
        );
        m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, rtv_handle);
        rtv_handle.Offset(m_rtv_descriptor_size);
    }

    return true;
}

bool Engine::immediate_submit(std::function<void(ID3D12GraphicsCommandList *cmd_list)> &&f)
{
    DXERR(
        m_immediate_submit.command_allocator->Reset(),
        "Engine::immediate_submit: failed to reset command allocator"
    );
    DXERR(
        m_immediate_submit.command_list->Reset(m_immediate_submit.command_allocator.Get(), nullptr),
        "Engine::immediate_submit: failed to reset command list"
    );

    f(m_immediate_submit.command_list.Get());

    DXERR(
        m_immediate_submit.command_list->Close(),
        "Engine::immediate_submit: failed to close command list"
    );

    std::array<ID3D12CommandList *const, 1> lists{m_immediate_submit.command_list.Get()};
    m_command_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    uint64_t wait_value;
    if (!signal_fence(m_immediate_submit.fence.Get(), m_immediate_submit.fence_value, wait_value))
    {
        spdlog::error("Engine::immediate_submit: failed to signal fence");
        return false;
    }

    if (!wait_for_fence_value(
            m_immediate_submit.fence.Get(),
            m_immediate_submit.fence_event,
            wait_value
        ))
    {
        spdlog::error("Engine::immediate_submit: failed to wait for fence");
        return false;
    }

    return true;
}

bool Engine::create_buffer(
    uint64_t size, D3D12_RESOURCE_STATES initial_state, D3D12_HEAP_TYPE heap_type,
    ComPtr<ID3D12Resource> &out_buffer
)
{
    CD3DX12_HEAP_PROPERTIES heap_props(heap_type);
    CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    DXERR(
        m_device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            initial_state,
            nullptr,
            IID_PPV_ARGS(&out_buffer)
        ),
        "Engine::create_buffer: failed to create buffer"
    );
    return true;
}

bool Engine::create_texture(
    uint64_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initial_state,
    ComPtr<ID3D12Resource> &out_texture, D3D12_RESOURCE_FLAGS flags
)
{
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
    resource_desc.Flags = flags;
    resource_desc.MipLevels = 1;
    DXERR(
        m_device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            initial_state,
            nullptr,
            IID_PPV_ARGS(&out_texture)
        ),
        "Engine::create_texture: failed to create texture"
    );

    return true;
}

bool Engine::create_depth_texture(
    uint64_t width, uint32_t height, ComPtr<ID3D12Resource> &out_texture
)
{
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height);
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resource_desc.MipLevels = 1;
    CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);
    DXERR(
        m_device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &resource_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear_value,
            IID_PPV_ARGS(&out_texture)
        ),
        "Engine::create_depth_texture: failed to create depth texture"
    );

    return true;
}

bool Engine::upload_to_buffer(
    ID3D12Resource *dst_buffer, D3D12_RESOURCE_STATES dst_buffer_state, void *src_data,
    uint64_t src_data_size
)
{
    ComPtr<ID3D12Resource> staging_buffer;
    if (!create_buffer(
            src_data_size,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_HEAP_TYPE_UPLOAD,
            staging_buffer
        ))
    {
        spdlog::error("Engine::upload_to_buffer: failed to create staging buffer");
        return false;
    }

    void *staging_buffer_ptr;
    DXERR(
        staging_buffer->Map(0, nullptr, &staging_buffer_ptr),
        "Engine::upload_to_buffer: failed to map staging buffer"
    );
    std::memcpy(staging_buffer_ptr, src_data, src_data_size);
    staging_buffer->Unmap(0, nullptr);

    bool res = immediate_submit(
        [dst_buffer, staging_buffer, dst_buffer_state](ID3D12GraphicsCommandList *cmd_list) {
            CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                dst_buffer,
                dst_buffer_state,
                D3D12_RESOURCE_STATE_COPY_DEST
            );
            cmd_list->ResourceBarrier(1, &barrier);

            cmd_list->CopyResource(dst_buffer, staging_buffer.Get());

            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                dst_buffer,
                D3D12_RESOURCE_STATE_COPY_DEST,
                dst_buffer_state
            );
            cmd_list->ResourceBarrier(1, &barrier);
        }
    );

    if (!res)
    {
        spdlog::error("Engine::upload_to_buffer: immediate submit failed");
        return false;
    }

    return true;
}

bool Engine::upload_to_texture(
    ID3D12Resource *dst_texture, D3D12_RESOURCE_STATES dst_texture_state, void *src_data,
    uint64_t width, uint64_t height, uint64_t channels
)
{
    uint64_t src_data_size = width * height * channels;
    ComPtr<ID3D12Resource> staging_buffer;
    if (!create_buffer(
            src_data_size,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_HEAP_TYPE_UPLOAD,
            staging_buffer
        ))
    {
        spdlog::error("Engine::upload_to_texture: failed to create staging buffer");
        return false;
    }

    bool res = immediate_submit([&](ID3D12GraphicsCommandList *cmd_list) {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            dst_texture,
            dst_texture_state,
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        cmd_list->ResourceBarrier(1, &barrier);

        D3D12_SUBRESOURCE_DATA data{
            .pData = src_data,
            .RowPitch = static_cast<LONG_PTR>(width * channels),
            .SlicePitch = static_cast<LONG_PTR>(width * height * channels),
        };
        UpdateSubresources(cmd_list, dst_texture, staging_buffer.Get(), 0, 0, 1, &data);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            dst_texture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            dst_texture_state
        );
        cmd_list->ResourceBarrier(1, &barrier);
    });

    if (!res)
    {
        spdlog::error("Engine::upload_to_texture: immediate submit failed");
        return false;
    }

    return true;
}

bool Engine::signal_fence(ID3D12Fence *fence, uint64_t &fence_value, uint64_t &out_wait_value)
{
    out_wait_value = ++fence_value;
    DXERR(
        m_command_queue->Signal(fence, out_wait_value),
        "Engine::signal_fence: failed to signal fence"
    );
    return true;
}

bool Engine::wait_for_fence_value(ID3D12Fence *fence, HANDLE fence_event, uint64_t value)
{
    if (fence->GetCompletedValue() < value)
    {
        DXERR(
            fence->SetEventOnCompletion(value, fence_event),
            "Engine::wait_for_fence_value: failed to set event"
        );
        std::chrono::milliseconds duration = std::chrono::milliseconds::max();
        WaitForSingleObject(fence_event, static_cast<DWORD>(duration.count()));
    }
    return true;
}

bool Engine::flush()
{
    uint64_t wait_value;
    DXERR(
        signal_fence(m_fence.Get(), m_fence_value, wait_value),
        "Engine::flush: failed to signal fence"
    );
    DXERR(
        wait_for_fence_value(m_fence.Get(), m_fence_event, wait_value),
        "Engine::flush: failed to wait for fence"
    );
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

bool compile_shader(LPCWSTR path, LPCSTR entry_point, LPCSTR target, ID3DBlob **code)
{
    UINT compile_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#if defined(_DEBUG)
    compile_flags |= D3DCOMPILE_DEBUG;
#endif

    ComPtr<ID3DBlob> errors;
    if (HRESULT res = D3DCompileFromFile(
            path,
            nullptr,
            nullptr,
            entry_point,
            target,
            compile_flags,
            0,
            code,
            &errors
        );
        FAILED(res))
    {
        if (errors)
        {
            spdlog::error(
                "compile_shader: failed to compile shader:\n{}",
                static_cast<const char *>(errors->GetBufferPointer())
            );
        }
        else
        {
            spdlog::error(
                "compile_shader: failed to compile shader: 0x{:x}",
                static_cast<unsigned long>(res)
            );
        }
        return false;
    }

    return true;
}
