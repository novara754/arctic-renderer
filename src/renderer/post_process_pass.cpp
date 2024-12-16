#include "post_process_pass.hpp"

#include <d3d12.h>
#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

namespace Arctic::Renderer
{

bool PostProcessPass::init()
{
    std::vector<uint8_t> cs_code;
    if (!m_rhi->compiler()
             .compile_shader(L"./shaders/post_process.hlsl", L"main", L"cs_6_6", cs_code))
    {
        spdlog::error("PostProcessPass::init: failed to compile shader");
        return false;
    }
    spdlog::trace("PostProcessPass::init: compiled shader");

    std::array<CD3DX12_ROOT_PARAMETER, 1> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init(
        static_cast<UINT>(root_parameters.size()),
        root_parameters.data(),
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
    );
    ComPtr<ID3DBlob> root_signature, error;
    if (FAILED(D3D12SerializeRootSignature(
            &root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &root_signature,
            &error
        )))
    {
        spdlog::error(
            "PostProcessPass::init: failed to serialize root signature: {}",
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
        "PostProcessPass::init: failed to create root signature"
    );
    spdlog::trace("PostProcessPass::init: created root signature");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = m_root_signature.Get();
    pipeline_desc.CS = {cs_code.data(), cs_code.size()};
    DXERR(
        m_rhi->device()->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "PostProcessPass::init: failed to create pipeline state"
    );

    return true;
}

void PostProcessPass::run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data)
{
    ZoneScoped;
    TracyD3D12Zone(m_rhi->tracy_ctx(), cmd_list, "Post Process Pass");

    ConstantBuffer constants{
        .input_idx = run_data.input_uav_idx,
        .output_idx = run_data.output_uav_idx,

        .gamma = run_data.gamma,
        .tm_method = run_data.tm_method,
        .exposure = run_data.exposure,
    };

    cmd_list->SetComputeRootSignature(m_root_signature.Get());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->SetComputeRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
    cmd_list->Dispatch(
        (run_data.viewport_width + GROUP_WIDTH - 1) / GROUP_WIDTH,
        (run_data.viewport_height + GROUP_HEIGHT - 1) / GROUP_HEIGHT,
        1
    );
}

} // namespace Arctic::Renderer
