#include "shadow_map_pass.hpp"

#include <array>

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

bool ShadowMapPass::init()
{
    ComPtr<ID3DBlob> vs_code;
    if (!compile_shader(L"../shaders/depth.hlsl", "main", "vs_5_0", &vs_code))
    {
        spdlog::error("ShadowMapPass::init: failed to compile depth shader");
        return false;
    }
    spdlog::trace("ShadowMapPass::init: compiled depth shader");

    if (!m_engine->create_texture(
            SIZE,
            SIZE,
            DXGI_FORMAT_R32_TYPELESS,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            m_depth_texture,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        ))
    {
        spdlog::error("ShadowMapPass::init: failed to create depth texture");
        return false;
    }

    if (!m_engine->create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            1,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_dsv_heap
        ))
    {
        spdlog::error("ShadowMapPass::init: failed to create dsv descriptor heap");
        return false;
    }
    spdlog::trace("ShadowMapPass::init: created dsv descriptor heap");

    D3D12_DEPTH_STENCIL_VIEW_DESC shadow_map_dsv{};
    shadow_map_dsv.Format = DXGI_FORMAT_D32_FLOAT;
    shadow_map_dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_engine->device()->CreateDepthStencilView(
        m_depth_texture.Get(),
        &shadow_map_dsv,
        m_dsv_heap->GetCPUDescriptorHandleForHeapStart()
    );

    ComPtr<ID3DBlob> root_signature;

    std::array<CD3DX12_ROOT_PARAMETER, 1> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
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
            nullptr
        ),
        "ShadowMapPass::init: failed to serialize root signature"
    );
    DXERR(
        m_engine->device()->CreateRootSignature(
            0,
            root_signature->GetBufferPointer(),
            root_signature->GetBufferSize(),
            IID_PPV_ARGS(&m_root_signature)
        ),
        "ShadowMapPass::init: failed to create root signature"
    );
    spdlog::trace("ShadowMapPass::init: created root signature");

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
    pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    pipeline_desc.SampleMask = ~0u;
    pipeline_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    pipeline_desc.RasterizerState.FrontCounterClockwise = TRUE;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    pipeline_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    pipeline_desc.InputLayout = {vertex_layout.data(), static_cast<UINT>(vertex_layout.size())};
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 0;
    pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipeline_desc.SampleDesc = {1, 0};
    DXERR(
        m_engine->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "ShadowMapPass::init: failed to create pipeline state"
    );
    spdlog::trace("ShadowMapPass::init: created pipeline state");

    return true;
}

void ShadowMapPass::run(ID3D12GraphicsCommandList *cmd_list, Scene &scene)
{
    ConstantBuffer constants{
        .view = scene.sun.view_matrix(),
        .proj = scene.sun.proj_matrix(),
    };

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = m_dsv_heap->GetCPUDescriptorHandleForHeapStart();

    cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmd_list->SetGraphicsRootSignature(m_root_signature.Get());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->SetGraphicsRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);

    cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd_list->OMSetRenderTargets(0, nullptr, FALSE, &dsv_handle);

    D3D12_VIEWPORT viewport{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(SIZE),
        .Height = static_cast<float>(SIZE),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    cmd_list->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{
        .left = 0,
        .top = 0,
        .right = static_cast<long>(SIZE),
        .bottom = static_cast<long>(SIZE),
    };
    cmd_list->RSSetScissorRects(1, &scissor);

    for (size_t mesh_idx : scene.objects)
    {
        const Mesh &mesh = scene.meshes[mesh_idx];
        cmd_list->IASetVertexBuffers(0, 1, &mesh.vertex_buffer_view);
        cmd_list->IASetIndexBuffer(&mesh.index_buffer_view);
        cmd_list->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
    }
}
