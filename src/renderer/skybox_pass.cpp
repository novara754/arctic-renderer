#include "skybox_pass.hpp"

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

namespace Arctic::Renderer
{

[[nodiscard]] bool SkyboxPass::init()
{
    std::vector<uint8_t> vs_code, ps_code;
    if (!m_rhi->compiler().compile_shader(L"./shaders/skybox.hlsl", L"vs_main", L"vs_6_6", vs_code))
    {
        spdlog::error("SkyboxPass::init: failed to compile vertex shader");
        return false;
    }
    if (!m_rhi->compiler().compile_shader(L"./shaders/skybox.hlsl", L"ps_main", L"ps_6_6", ps_code))
    {
        spdlog::error("SkyboxPass::init: failed to compile pixel shader");
        return false;
    }
    spdlog::trace("SkyboxPass::init: compiled shaders");

    ComPtr<ID3DBlob> root_signature;

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
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
    );
    DXERR(
        D3D12SerializeRootSignature(
            &root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &root_signature,
            nullptr
        ),
        "SkyboxPass::init: failed to serialize root signature"
    );
    DXERR(
        m_rhi->device()->CreateRootSignature(
            0,
            root_signature->GetBufferPointer(),
            root_signature->GetBufferSize(),
            IID_PPV_ARGS(&m_root_signature)
        ),
        "SkyboxPass::init: failed to create root signature"
    );
    spdlog::trace("SkyboxPass::init: created root signature");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = m_root_signature.Get();
    pipeline_desc.VS = {vs_code.data(), vs_code.size()};
    pipeline_desc.PS = {ps_code.data(), ps_code.size()};
    pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
    pipeline_desc.RasterizerState.FrontCounterClockwise = TRUE;
    pipeline_desc.SampleMask = ~0u;
    pipeline_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
    pipeline_desc.RasterizerState.FrontCounterClockwise = TRUE;
    pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pipeline_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
    pipeline_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.NumRenderTargets = 1;
    pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipeline_desc.SampleDesc = {1, 0};
    DXERR(
        m_rhi->device()->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "SkyboxPass::init: failed to create pipeline state"
    );
    spdlog::trace("SkyboxPass::init: created pipeline state");

    return true;
}

void SkyboxPass::run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data)
{
    ZoneScoped;
    TracyD3D12Zone(m_rhi->tracy_ctx(), cmd_list, "Skybox Pass");

    ConstantBuffer constants{
        .environment_idx = run_data.environment_srv_idx,
        .proj_view = run_data.camera.proj_view_matrix_no_translation(),
    };

    cmd_list->SetGraphicsRootSignature(m_root_signature.Get());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd_list->OMSetRenderTargets(1, &run_data.color_target_rtv, FALSE, &run_data.depth_target_rtv);

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

    cmd_list->SetGraphicsRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
    cmd_list->DrawInstanced(36, 1, 0, 0);
}

} // namespace Arctic::Renderer
