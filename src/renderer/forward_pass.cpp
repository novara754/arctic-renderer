#include "forward_pass.hpp"

#include <d3d12.h>
#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

namespace Arctic::Renderer
{

bool ForwardPass::init()
{
    std::vector<uint8_t> vs_code, ps_code;
    if (!m_rhi->compiler()
             .compile_shader(L"./shaders/forward.hlsl", L"vs_main", L"vs_6_6", vs_code))
    {
        spdlog::error("ForwardPass::init: failed to compile vertex shader");
        return false;
    }
    if (!m_rhi->compiler()
             .compile_shader(L"./shaders/forward.hlsl", L"ps_main", L"ps_6_6", ps_code))
    {
        spdlog::error("ForwardPass::init: failed to compile pixel shader");
        return false;
    }
    spdlog::trace("ForwardPass::init: compiled forward shaders");

    ComPtr<ID3DBlob> root_signature;
    ComPtr<ID3DBlob> error;

    std::array<CD3DX12_ROOT_PARAMETER, 1> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);

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
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
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
        m_rhi->device()->CreateRootSignature(
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
            .SemanticName = "TANGENT",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0,
            .AlignedByteOffset = offsetof(Vertex, tangent),
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "BITANGENT",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0,
            .AlignedByteOffset = offsetof(Vertex, bitangent),
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
    pipeline_desc.VS = {vs_code.data(), vs_code.size()};
    pipeline_desc.PS = {ps_code.data(), ps_code.size()};
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
        m_rhi->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "ForwardPass::init: failed to create pipeline state"
    );
    spdlog::trace("ForwardPass::init: created pipeline state");

    return true;
}

void ForwardPass::run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data)
{
    ZoneScoped;
    TracyD3D12Zone(m_rhi->tracy_ctx(), cmd_list, "Forward Pass");

    ConstantBuffer constants{
        .eye = run_data.scene.camera.eye,
        .proj_view = run_data.scene.camera.proj_view_matrix(),
        .light_proj_view = run_data.scene.sun.proj_view_matrix(),
        .sun_dir = run_data.scene.sun.direction(),
        .ambient = run_data.scene.ambient,
        .sun_color = run_data.scene.sun.color,

        .shadow_map_idx = run_data.shadow_map_srv_idx,
        .environment_idx = run_data.environment_srv_idx,
        .lights_buffer_idx = run_data.lights_buffer_cbv_idx,
    };

    cmd_list->ClearDepthStencilView(
        run_data.depth_target_dsv,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr
    );

    cmd_list->SetGraphicsRootSignature(m_root_signature.Get());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd_list->OMSetRenderTargets(1, &run_data.color_target_rtv, FALSE, &run_data.depth_target_dsv);

    D3D12_VIEWPORT viewport{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(run_data.viewport_width),
        .Height = static_cast<float>(run_data.viewport_height),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    cmd_list->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{
        .left = 0,
        .top = 0,
        .right = static_cast<long>(run_data.viewport_width),
        .bottom = static_cast<long>(run_data.viewport_height),
    };
    cmd_list->RSSetScissorRects(1, &scissor);

    {
        ZoneScopedN("Draw Loop");
        for (const Object &obj : run_data.scene.objects)
        {
            const Mesh &mesh = run_data.meshes[obj.mesh_idx];
            const Material &material = run_data.materials[mesh.material_idx];
            constants.model = obj.trs;
            constants.material_offset = material.srv_offset;

            cmd_list
                ->SetGraphicsRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
            cmd_list->IASetVertexBuffers(0, 1, &mesh.vertex_buffer_view);
            cmd_list->IASetIndexBuffer(&mesh.index_buffer_view);
            cmd_list->DrawIndexedInstanced(mesh.index_count, 1, 0, 0, 0);
        }
    }
}

} // namespace Arctic::Renderer
