#include "forward_pass.hpp"

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

bool ForwardPass::init(uint32_t width, uint32_t height, ID3D12Resource *shadow_map)
{
    m_output_size.width = width;
    m_output_size.height = height;

    if (!m_engine->create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            1,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_rtv_heap
        ))
    {
        spdlog::error("ForwardPass::init: failed to create rtv descriptor heap");
        return false;
    }
    spdlog::trace("ForwardPass::init: created rtv descriptor heap");

    if (!m_engine->create_texture(
            width,
            height,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            m_color_target,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        ))
    {
        spdlog::error("ForwardPass::init: failed to create color target texture");
        return false;
    }

    m_engine->device()->CreateRenderTargetView(
        m_color_target.Get(),
        nullptr,
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart()
    );

    // ------------
    // Read and compile shaders
    // -------
    ComPtr<ID3DBlob> vs_code, ps_code;
    if (!compile_shader(L"../shaders/forward.hlsl", "vs_main", "vs_5_0", &vs_code))
    {
        spdlog::error("ForwardPass::init: failed to compile vertex shader");
        return false;
    }
    if (!compile_shader(L"../shaders/forward.hlsl", "ps_main", "ps_5_0", &ps_code))
    {
        spdlog::error("ForwardPass::init: failed to compile pixel shader");
        return false;
    }
    spdlog::trace("ForwardPass::init: compiled forward shaders");

    // ------------
    // Create depth texture and DSV heap
    // -------
    {
        if (!m_engine->create_depth_texture(width, height, m_depth_texture))
        {
            spdlog::error("ForwardPass::init: failed to create depth texture");
            return false;
        }

        if (!m_engine->create_descriptor_heap(
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                1,
                D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
                m_dsv_heap
            ))
        {
            spdlog::error("ForwardPass::init: failed to create dsv descriptor heap");
            return false;
        }
        spdlog::trace("ForwardPass::init: created dsv descriptor heap");

        m_dsv_descriptor_size =
            m_engine->device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        m_engine->device()->CreateDepthStencilView(
            m_depth_texture.Get(),
            nullptr,
            m_dsv_heap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // ------------
    // Create SRV heap
    // -------
    {
        if (!m_engine->create_descriptor_heap(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                50,
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                m_srv_heap
            ))
        {
            spdlog::error("ForwardPass::init: failed to create srv descriptor heap");
            return false;
        }
        spdlog::trace("ForwardPass::init: created srv descriptor heap");

        m_srv_descriptor_size = m_engine->device()->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        D3D12_SHADER_RESOURCE_VIEW_DESC shadow_map_srv_desc{};
        shadow_map_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadow_map_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        shadow_map_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shadow_map_srv_desc.Texture2D.MipLevels = 1;
        m_engine->device()->CreateShaderResourceView(
            shadow_map,
            &shadow_map_srv_desc,
            m_srv_heap->GetCPUDescriptorHandleForHeapStart()
        );
    }

    // ------------
    // Create root signature
    // -------
    ComPtr<ID3DBlob> root_signature;
    ComPtr<ID3DBlob> error;

    CD3DX12_DESCRIPTOR_RANGE shadow_map_range;
    shadow_map_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE material_range;
    material_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, 1);

    std::array<CD3DX12_ROOT_PARAMETER, 3> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);
    root_parameters[1].InitAsDescriptorTable(1, &shadow_map_range);
    root_parameters[2].InitAsDescriptorTable(1, &material_range);

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init(
        static_cast<UINT>(root_parameters.size()),
        root_parameters.data(),
        1,
        &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );
    if (FAILED(D3D12SerializeRootSignature(
            &root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &root_signature,
            &error
        )))
    {
        spdlog::error(
            "ForwardPass::init: failed to serialize root signature: {}",
            static_cast<char *>(error->GetBufferPointer())
        );
        return false;
    }
    DXERR(
        m_engine->device()->CreateRootSignature(
            0,
            root_signature->GetBufferPointer(),
            root_signature->GetBufferSize(),
            IID_PPV_ARGS(&m_root_signature)
        ),
        "ForwardPass::init: failed to create root signature"
    );
    spdlog::trace("ForwardPass::init: created root signature");

    // ------------
    // Create graphics pipeline state
    // -------
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
    pipeline_desc.pRootSignature = m_root_signature.Get();
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
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipeline_desc.SampleDesc = {1, 0};
    DXERR(
        m_engine->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "ForwardPass::init: failed to create pipeline state"
    );
    spdlog::trace("ForwardPass::init: created pipeline state");

    return true;
}

bool ForwardPass::resize(uint32_t new_width, uint32_t new_height)
{
    m_depth_texture.Reset();
    if (!m_engine->create_depth_texture(new_width, new_height, m_depth_texture))
    {
        spdlog::error("ForwardPass::resize: failed to create depth texture");
        return false;
    }

    m_engine->device()->CreateDepthStencilView(
        m_depth_texture.Get(),
        nullptr,
        m_dsv_heap->GetCPUDescriptorHandleForHeapStart()
    );

    m_output_size.width = new_width;
    m_output_size.height = new_height;

    return true;
}

void ForwardPass::run(ID3D12GraphicsCommandList *cmd_list, const Scene &scene)
{
    ConstantBuffer constants{
        .proj_view = scene.camera.proj_view_matrix(),
        .light_proj_view = scene.sun.proj_view_matrix(),
        .sun_dir = scene.sun.direction(),
        .ambient = scene.ambient,
        .sun_color = scene.sun.color,
    };

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = m_dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE shadow_map_srv_handle =
        m_srv_heap->GetGPUDescriptorHandleForHeapStart();

    std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 1.0f};
    cmd_list->ClearRenderTargetView(rtv_handle, clear_color.data(), 0, nullptr);
    cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmd_list->SetGraphicsRootSignature(m_root_signature.Get());
    std::array heaps{m_srv_heap.Get()};
    cmd_list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->SetGraphicsRootDescriptorTable(1, shadow_map_srv_handle);

    cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);

    D3D12_VIEWPORT viewport{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(m_output_size.width),
        .Height = static_cast<float>(m_output_size.height),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    cmd_list->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{
        .left = 0,
        .top = 0,
        .right = static_cast<long>(m_output_size.width),
        .bottom = static_cast<long>(m_output_size.height),
    };
    cmd_list->RSSetScissorRects(1, &scissor);

    for (const Object &obj : scene.objects)
    {
        const Mesh &mesh = scene.meshes[obj.mesh_idx];
        constants.model = obj.trs;

        CD3DX12_GPU_DESCRIPTOR_HANDLE diffuse_srv_handle(
            m_srv_heap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(mesh.material_idx),
            m_srv_descriptor_size
        );
        cmd_list->SetGraphicsRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
        cmd_list->SetGraphicsRootDescriptorTable(2, diffuse_srv_handle);
        cmd_list->IASetVertexBuffers(0, 1, &mesh.vertex_buffer_view);
        cmd_list->IASetIndexBuffer(&mesh.index_buffer_view);
        cmd_list->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
    }
}

void ForwardPass::create_srv_tex2d(int32_t index, ID3D12Resource *resource, DXGI_FORMAT format)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle(
        m_srv_heap->GetCPUDescriptorHandleForHeapStart(),
        index + 1,
        m_srv_descriptor_size
    );
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    m_engine->device()->CreateShaderResourceView(resource, &srv_desc, srv_handle);
}
