#include "app.hpp"

#include <chrono>

#include <SDL3/SDL_events.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

bool get_best_adapter(
    ComPtr<IDXGIFactory4> dxgi_factory4, ComPtr<IDXGIAdapter4> &out_dxgi_adapter4
);

bool has_tearing_support(ComPtr<IDXGIFactory4> dxgi_factory4);

bool compile_shader(LPCWSTR path, LPCSTR entry_point, LPCSTR target, ID3DBlob **code);

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
        D3D12CreateDevice(dxgi_adapter4.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)),
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
    {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window),
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
    if (!create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            NUM_FRAMES,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_rtv_descriptor_heap
        ))
    {
        spdlog::error("App::init: failed to create rtv descriptor heap");
        return false;
    }
    spdlog::trace("App::init: created rtv descriptor heap");

    // ------------
    // Create RTVs
    // -------
    if (!update_render_target_views())
    {
        spdlog::error("App::init: failed to create rtvs");
        return false;
    }
    spdlog::trace("App::init: created rtvs");

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

    // ------------
    // Create triange pipeline state
    // -------
    {
        ComPtr<ID3DBlob> vs_code, ps_code;
        if (!compile_shader(L"../shaders/triangle.hlsl", "vs_main", "vs_5_0", &vs_code))
        {
            spdlog::error("App::init: failed to compile vertex shader");
            return false;
        }
        if (!compile_shader(L"../shaders/triangle.hlsl", "ps_main", "ps_5_0", &ps_code))
        {
            spdlog::error("App::init: failed to compile pixel shader");
            return false;
        }
        spdlog::trace("App::init: compiled triangle shaders");

        ComPtr<ID3DBlob> root_signature;
        ComPtr<ID3DBlob> error;
        CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
        std::array root_parameters{
            D3D12_ROOT_PARAMETER{
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
                .Constants =
                    {
                        .ShaderRegister = 0,
                        .RegisterSpace = 0,
                        .Num32BitValues = 3,
                    }
            },
        };
        root_signature_desc.Init(
            static_cast<UINT>(root_parameters.size()),
            root_parameters.data(),
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_NONE
        );
        DXERR(
            D3D12SerializeRootSignature(
                &root_signature_desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &root_signature,
                &error
            ),
            "App::init: failed to serialize triangle root signature"
        );
        DXERR(
            m_device->CreateRootSignature(
                0,
                root_signature->GetBufferPointer(),
                root_signature->GetBufferSize(),
                IID_PPV_ARGS(&m_triangle_root_signature)
            ),
            "App::init: failed to create triangle root signature"
        );
        spdlog::trace("App::init: created triangle root signature");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = m_triangle_root_signature.Get();
        pipeline_desc.VS = {
            .pShaderBytecode = vs_code->GetBufferPointer(),
            .BytecodeLength = vs_code->GetBufferSize(),
        };
        pipeline_desc.PS = {
            .pShaderBytecode = ps_code->GetBufferPointer(),
            .BytecodeLength = ps_code->GetBufferSize(),
        };
        pipeline_desc.BlendState.RenderTarget[0] = {
            .BlendEnable = FALSE,
            .LogicOpEnable = FALSE,
            .SrcBlend = D3D12_BLEND_ONE,
            .DestBlend = D3D12_BLEND_ZERO,
            .BlendOp = D3D12_BLEND_OP_ADD,
            .SrcBlendAlpha = D3D12_BLEND_ONE,
            .DestBlendAlpha = D3D12_BLEND_ZERO,
            .BlendOpAlpha = D3D12_BLEND_OP_ADD,
            .LogicOp = D3D12_LOGIC_OP_NOOP,
            .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
        };
        pipeline_desc.SampleMask = ~0u;
        pipeline_desc.RasterizerState = {
            .FillMode = D3D12_FILL_MODE_SOLID,
            .CullMode = D3D12_CULL_MODE_NONE,
            .FrontCounterClockwise = TRUE,
            .DepthBias = 0,
            .DepthBiasClamp = 0.0f,
            .SlopeScaledDepthBias = 0.0f,
            .DepthClipEnable = TRUE,
            .MultisampleEnable = FALSE,
            .AntialiasedLineEnable = FALSE,
            .ForcedSampleCount = 0,
            .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
        };
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc = {1, 0};
        DXERR(
            m_device
                ->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&m_triangle_pipeline)),
            "App::init: failed to create triangle pipeline state"
        );
        spdlog::trace("App::init: created triangle pipeline state");
    }

    // ------------
    // Initialize ImGui
    // -------
    {
        if (!create_descriptor_heap(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                1,
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                m_imgui_cbv_srv_heap
            ))
        {
            spdlog::error("App::init: failed to create cbv srv heap for imgui");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;

        ImGui_ImplSDL3_InitForOther(m_window);
        ImGui_ImplDX12_Init(
            m_device.Get(),
            NUM_FRAMES,
            swapchain_desc.Format,
            m_imgui_cbv_srv_heap.Get(),
            m_imgui_cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(),
            m_imgui_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart()
        );
        spdlog::trace("App::init: initialized imgui");
    }

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
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                if (!handle_resize())
                {
                    spdlog::error("App::run: resize was requested but failed");
                    break;
                };
            }

            ImGui_ImplSDL3_ProcessEvent(&event);
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

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

bool App::render_frame()
{
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    build_ui();

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
        std::array<float, 4>
            clear_color{m_background_color[0], m_background_color[1], m_background_color[2], 1.0};
        m_command_list->ClearRenderTargetView(rtv_handle, clear_color.data(), 0, nullptr);

        m_command_list->SetGraphicsRootSignature(m_triangle_root_signature.Get());
        m_command_list->SetGraphicsRoot32BitConstants(0, 3, m_top_vertex_color.data(), 0);
        m_command_list->SetPipelineState(m_triangle_pipeline.Get());
        m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        D3D12_VIEWPORT viewport{
            .TopLeftX = 0.0f,
            .TopLeftY = 0.0f,
            .Width = static_cast<float>(m_window_size.width),
            .Height = static_cast<float>(m_window_size.height),
            .MinDepth = 0.0f,
            .MaxDepth = 1.0f,
        };
        m_command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor{
            .left = 0,
            .top = 0,
            .right = m_window_size.width,
            .bottom = m_window_size.height,
        };
        m_command_list->RSSetScissorRects(1, &scissor);
        m_command_list->DrawInstanced(3, 1, 0, 0);

        ImGui::Render();
        std::array descriptor_heaps{m_imgui_cbv_srv_heap.Get()};
        m_command_list->SetDescriptorHeaps(1, descriptor_heaps.data());
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_command_list.Get());
    }

    {

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backbuffer.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        m_command_list->ResourceBarrier(1, &barrier);

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

void App::build_ui()
{
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
        ImGui::ColorEdit3("Background Color", m_background_color.data());
        ImGui::ColorEdit3("Top Vertex Color", m_top_vertex_color.data());
    }
    ImGui::End();
}

bool App::handle_resize()
{
    int width, height;
    assert(SDL_GetWindowSize(m_window, &width, &height));

    if (width == m_window_size.width && height == m_window_size.height)
    {
        return true;
    }

    m_window_size.width = std::min(1, width);
    m_window_size.height = std::min(1, height);

    if (!flush())
    {
        spdlog::error("App::handle_resize: failed to flush");
        return false;
    }

    for (int i = 0; i < NUM_FRAMES; ++i)
    {
        m_backbuffers[i].Reset();
        m_frame_fence_values[i] = m_frame_fence_values[m_current_backbuffer_index];
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
    DXERR(
        m_swapchain->GetDesc(&swap_chain_desc),
        "App::handle_resize: failed to get previous swapchain description"
    );
    DXERR(
        m_swapchain->ResizeBuffers(
            NUM_FRAMES,
            m_window_size.width,
            m_window_size.height,
            swap_chain_desc.BufferDesc.Format,
            swap_chain_desc.Flags
        ),
        "App::handle_resize: failed to resize buffers"
    );
    m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();

    if (!update_render_target_views())
    {
        spdlog::error("App::handle_resize: failed to update rtvs");
        return false;
    }

    return true;
}

bool App::create_descriptor_heap(
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
        "App::create_descriptor_heap: failed to create descriptor heap"
    );

    return true;
}

bool App::update_render_target_views()
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
            "App::update_render_target_views: failed to get swapchain buffer"
        );
        m_device->CreateRenderTargetView(m_backbuffers[i].Get(), nullptr, rtv_handle);
        rtv_handle.Offset(m_rtv_descriptor_size);
    }

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
