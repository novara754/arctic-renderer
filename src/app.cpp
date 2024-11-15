#include "app.hpp"

#include <chrono>
#include <cstddef>

#include <SDL3/SDL_events.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
        .Width = static_cast<UINT>(m_window_size.width),
        .Height = static_cast<UINT>(m_window_size.height),
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
    // Create objects for immediate submit
    // -------
    {
        DXERR(
            m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_immediate_submit.command_allocator)
            ),
            "App::init: failed to create command allocator for immediate submit"
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
            "App::init: failed to create command list for immediate submit"
        );
        m_immediate_submit.command_list->SetName(L"immediate submit command list");
        DXERR(
            m_immediate_submit.command_list->Close(),
            "App::init: failed to close command list for immediate submit"
        );

        DXERR(
            m_device
                ->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_immediate_submit.fence)),
            "App::init: failed to create fence for immediate submit"
        );
        m_immediate_submit.fence->SetName(L"immediate submit command fence");
        m_immediate_submit.fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_immediate_submit.fence_event)
        {
            spdlog::error("App::init: failed to create fence event for immediate submit");
            return false;
        }

        spdlog::trace("App::init: created immediate submit objects");
    }

    // ------------
    // Create triangle pipeline state
    // -------
    {
        if (!load_scene(m_scene_path, m_scene))
        {
            spdlog::error("App::init: failed to load scene");
            return false;
        }

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

        {
            if (!create_depth_texture(m_window_size.width, m_window_size.height, m_depth_texture))
            {
                spdlog::error("App::init: failed to create depth texture");
                return false;
            }

            if (!create_descriptor_heap(
                    D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                    1,
                    D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                    m_dsv_descriptor_heap
                ))
            {
                spdlog::error("App::init: failed to create dsv descriptor heap");
                return false;
            }
            spdlog::trace("App::init: created dsv descriptor heap");

            m_dsv_descriptor_size =
                m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

            m_device->CreateDepthStencilView(
                m_depth_texture.Get(),
                nullptr,
                m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart()
            );
        }

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
                        .Num32BitValues = 2 * 4 * 4, // 2 float4x4
                    }
            },
        };
        root_signature_desc.Init(
            static_cast<UINT>(root_parameters.size()),
            root_parameters.data(),
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
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

        std::array vertex_layout{
            D3D12_INPUT_ELEMENT_DESC{
                .SemanticName = "POSITION",
                .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32B32_FLOAT,
                .InputSlot = 0,
                .AlignedByteOffset = offsetof(Vertex, position),
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                .InstanceDataStepRate = 0,
            },
            D3D12_INPUT_ELEMENT_DESC{
                .SemanticName = "NORMAL",
                .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32B32_FLOAT,
                .InputSlot = 0,
                .AlignedByteOffset = offsetof(Vertex, normal),
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                .InstanceDataStepRate = 0,
            },
            D3D12_INPUT_ELEMENT_DESC{
                .SemanticName = "TEXCOORD",
                .SemanticIndex = 0,
                .Format = DXGI_FORMAT_R32G32_FLOAT,
                .InputSlot = 0,
                .AlignedByteOffset = offsetof(Vertex, tex_coords),
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                .InstanceDataStepRate = 0,
            },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = m_triangle_root_signature.Get();
        pipeline_desc.VS = CD3DX12_SHADER_BYTECODE(vs_code.Get());
        pipeline_desc.PS = CD3DX12_SHADER_BYTECODE(ps_code.Get());
        pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
        pipeline_desc.SampleMask = ~0u;
        pipeline_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
        pipeline_desc.RasterizerState.FrontCounterClockwise = TRUE;
        pipeline_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
        pipeline_desc.InputLayout = {vertex_layout.data(), static_cast<UINT>(vertex_layout.size())};
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
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

bool App::load_scene(const std::string &path, Scene &out_scene)
{
    Assimp::Importer importer;

    const aiScene *scene =
        importer.ReadFile(path.c_str(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
    if (scene == nullptr)
    {
        spdlog::error("App::load_scene: failed to load file");
        return false;
    }

    if (scene->mRootNode == nullptr)
    {
        spdlog::error("App::load_scene: file has no root node");
        return false;
    }

    for (size_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
    {
        const aiMesh *ai_mesh = scene->mMeshes[mesh_idx];

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (size_t vertex_idx = 0; vertex_idx < ai_mesh->mNumVertices; ++vertex_idx)
        {
            Vertex vertex{
                .position =
                    {
                        ai_mesh->mVertices[vertex_idx].x,
                        ai_mesh->mVertices[vertex_idx].y,
                        ai_mesh->mVertices[vertex_idx].z,
                    },
                .normal =
                    {
                        ai_mesh->mNormals[vertex_idx].x,
                        ai_mesh->mNormals[vertex_idx].y,
                        ai_mesh->mNormals[vertex_idx].z,
                    },
                .tex_coords =
                    {
                        ai_mesh->mTextureCoords[0][vertex_idx].x,
                        ai_mesh->mTextureCoords[0][vertex_idx].y,
                    },
            };
            vertices.emplace_back(vertex);
        }

        for (size_t face_idx = 0; face_idx < ai_mesh->mNumFaces; ++face_idx)
        {
            const aiFace &face = ai_mesh->mFaces[face_idx];
            for (size_t index_idx = 0; index_idx < face.mNumIndices; ++index_idx)
            {
                indices.emplace_back(static_cast<uint32_t>(face.mIndices[index_idx]));
            }
        }

        bool res = true;

        Mesh mesh;

        uint64_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
        res &= create_buffer(
            vertex_buffer_size,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            D3D12_HEAP_TYPE_DEFAULT,
            mesh.vertex_buffer
        );

        res &= upload_to_resource(
            mesh.vertex_buffer.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            vertices.data(),
            vertex_buffer_size
        );

        uint64_t index_buffer_size = indices.size() * sizeof(uint32_t);
        res &= create_buffer(
            index_buffer_size,
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            D3D12_HEAP_TYPE_DEFAULT,
            mesh.index_buffer
        );

        res &= upload_to_resource(
            mesh.index_buffer.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            indices.data(),
            index_buffer_size
        );

        if (!res)
        {
            spdlog::error(
                "App::load_scene: failed to create vertex and/or index buffers for mesh #{}",
                mesh_idx
            );
        }

        mesh.vertex_buffer_view.BufferLocation = mesh.vertex_buffer->GetGPUVirtualAddress();
        mesh.vertex_buffer_view.StrideInBytes = sizeof(Vertex);
        mesh.vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertex_buffer_size);

        mesh.index_buffer_view.BufferLocation = mesh.index_buffer->GetGPUVirtualAddress();
        mesh.index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
        mesh.index_buffer_view.SizeInBytes = static_cast<UINT>(index_buffer_size);

        mesh.index_count = static_cast<uint32_t>(indices.size());

        out_scene.meshes.emplace_back(mesh);
    }

    std::vector nodes_to_process{scene->mRootNode};
    while (!nodes_to_process.empty())
    {
        const aiNode *node = nodes_to_process.back();
        nodes_to_process.pop_back();

        for (size_t i = 0; i < node->mNumChildren; ++i)
        {
            nodes_to_process.emplace_back(node->mChildren[i]);
        }

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            out_scene.objects.emplace_back(node->mMeshes[i]);
        }
    }

    return true;
}

bool App::render_frame()
{
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    build_ui();

    m_current_backbuffer_index = m_swapchain->GetCurrentBackBufferIndex();
    DXERR(
        wait_for_fence_value(
            m_fence.Get(),
            m_fence_event,
            m_frame_fence_values[m_current_backbuffer_index]
        ),
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
        struct
        {
            DirectX::XMFLOAT4X4 view;
            DirectX::XMFLOAT4X4 proj;
        } camera_matrices{
            .view = m_scene.camera.view_matrix(),
            .proj = m_scene.camera.proj_matrix(),
        };
        assert(sizeof(camera_matrices) == 2 * 4 * 4 * 4);

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
            m_rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
            m_current_backbuffer_index,
            m_rtv_descriptor_size
        );
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(
            m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart()
        );

        std::array<float, 4>
            clear_color{m_background_color[0], m_background_color[1], m_background_color[2], 1.0};
        m_command_list->ClearRenderTargetView(rtv_handle, clear_color.data(), 0, nullptr);
        m_command_list
            ->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        m_command_list->SetGraphicsRootSignature(m_triangle_root_signature.Get());
        m_command_list->SetPipelineState(m_triangle_pipeline.Get());
        m_command_list->SetGraphicsRoot32BitConstants(0, 2 * 4 * 4, &camera_matrices, 0);
        m_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
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

        for (size_t mesh_idx : m_scene.objects)
        {
            m_command_list->IASetVertexBuffers(0, 1, &m_scene.meshes[mesh_idx].vertex_buffer_view);
            m_command_list->IASetIndexBuffer(&m_scene.meshes[mesh_idx].index_buffer_view);
            m_command_list->DrawIndexedInstanced(m_scene.meshes[mesh_idx].index_count, 1, 0, 0, 0);
        }

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
            signal_fence(
                m_fence.Get(),
                m_fence_value,
                m_frame_fence_values[m_current_backbuffer_index]
            ),
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

        ImGui::SeparatorText("Camera");
        ImGui::DragFloat3("Eye", &m_scene.camera.eye.x, 0.1f);
        ImGui::DragFloat3("Rot", &m_scene.camera.rotation.x, 0.1f);
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

    m_window_size.width = std::max(1, width);
    m_window_size.height = std::max(1, height);

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

    m_depth_texture.Reset();
    if (!create_depth_texture(m_window_size.width, m_window_size.height, m_depth_texture))
    {
        spdlog::error("App::handle_resize: failed to create depth texture");
        return false;
    }

    m_device->CreateDepthStencilView(
        m_depth_texture.Get(),
        nullptr,
        m_dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart()
    );

    m_scene.camera.aspect =
        static_cast<float>(m_window_size.width) / static_cast<float>(m_window_size.height);

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

bool App::immediate_submit(std::function<void(ID3D12GraphicsCommandList *cmd_list)> &&f)
{
    DXERR(
        m_immediate_submit.command_allocator->Reset(),
        "App::immediate_submit: failed to reset command allocator"
    );
    DXERR(
        m_immediate_submit.command_list->Reset(m_immediate_submit.command_allocator.Get(), nullptr),
        "App::immediate_submit: failed to reset command list"
    );

    f(m_immediate_submit.command_list.Get());

    DXERR(
        m_immediate_submit.command_list->Close(),
        "App::immediate_submit: failed to close command list"
    );

    std::array<ID3D12CommandList *const, 1> lists{m_immediate_submit.command_list.Get()};
    m_command_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    uint64_t wait_value;
    if (!signal_fence(m_immediate_submit.fence.Get(), m_immediate_submit.fence_value, wait_value))
    {
        spdlog::error("App::immediate_submit: failed to signal fence");
        return false;
    }

    if (!wait_for_fence_value(
            m_immediate_submit.fence.Get(),
            m_immediate_submit.fence_event,
            wait_value
        ))
    {
        spdlog::error("App::immediate_submit: failed to wait for fence");
        return false;
    }

    return true;
}

bool App::create_buffer(
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
        "App::create_buffer: failed to create buffer"
    );
    return true;
}

bool App::create_depth_texture(uint64_t width, uint32_t height, ComPtr<ID3D12Resource> &out_texture)
{
    CD3DX12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height);
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
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
        "App::create_depth_texture: failed to create depth texture"
    );

    return true;
}

bool App::upload_to_resource(
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
        spdlog::error("App::upload_to_resource: failed to create staging buffer");
        return false;
    }

    void *staging_buffer_ptr;
    DXERR(
        staging_buffer->Map(0, nullptr, &staging_buffer_ptr),
        "App::upload_to_resource: failed to map staging buffer"
    );
    spdlog::debug(
        "src_data = {}, src_data_size = {}",
        spdlog::fmt_lib::ptr(src_data),
        src_data_size
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
        spdlog::error("App::upload_to_resource: immediate submit failed");
        return false;
    }

    return true;
}

bool App::signal_fence(ID3D12Fence *fence, uint64_t &fence_value, uint64_t &out_wait_value)
{
    out_wait_value = ++fence_value;
    DXERR(
        m_command_queue->Signal(fence, out_wait_value),
        "App::signal_fence: failed to signal fence"
    );
    return true;
}

bool App::wait_for_fence_value(ID3D12Fence *fence, HANDLE fence_event, uint64_t value)
{
    if (fence->GetCompletedValue() < value)
    {
        DXERR(
            fence->SetEventOnCompletion(value, fence_event),
            "App::wait_for_fence_value: failed to set event"
        );
        std::chrono::milliseconds duration = std::chrono::milliseconds::max();
        WaitForSingleObject(fence_event, static_cast<DWORD>(duration.count()));
    }
    return true;
}

bool App::flush()
{
    uint64_t wait_value;
    DXERR(
        signal_fence(m_fence.Get(), m_fence_value, wait_value),
        "App::flush: failed to signal fence"
    );
    DXERR(
        wait_for_fence_value(m_fence.Get(), m_fence_event, wait_value),
        "App::flush: failed to wait for fence"
    );
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
