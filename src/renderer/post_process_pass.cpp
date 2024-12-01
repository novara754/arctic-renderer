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
    ComPtr<ID3DBlob> cs_code;
    if (!compile_shader(L"./shaders/post_process.hlsl", "main", "cs_5_0", &cs_code))
    {
        spdlog::error("PostProcessPass::init: failed to compile shader");
        return false;
    }
    spdlog::trace("PostProcessPass::init: compiled shader");

    CD3DX12_DESCRIPTOR_RANGE descriptor_range{};
    descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

    std::array<CD3DX12_ROOT_PARAMETER, 2> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);
    root_parameters[1].InitAsDescriptorTable(1, &descriptor_range);

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc
        .Init(static_cast<UINT>(root_parameters.size()), root_parameters.data(), 0, nullptr);
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
    pipeline_desc.CS = CD3DX12_SHADER_BYTECODE(cs_code.Get());
    DXERR(
        m_rhi->device()->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "PostProcessPass::init: failed to create pipeline state"
    );

    return true;
}

void PostProcessPass::run(
    ID3D12GraphicsCommandList *cmd_list, D3D12_GPU_DESCRIPTOR_HANDLE descriptor_handle,
    uint32_t width, uint32_t height, uint32_t tm_method, float gamma, float exposure
)
{
    ZoneScoped;
    TracyD3D12Zone(m_rhi->tracy_ctx(), cmd_list, "Post Process Pass");

    ConstantBuffer constants{
        .gamma = gamma,
        .tm_method = tm_method,
        .exposure = exposure,
    };

    cmd_list->SetComputeRootSignature(m_root_signature.Get());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->SetComputeRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
    cmd_list->SetComputeRootDescriptorTable(1, descriptor_handle);
    cmd_list->Dispatch(
        (width + GROUP_WIDTH - 1) / GROUP_WIDTH,
        (height + GROUP_HEIGHT - 1) / GROUP_HEIGHT,
        1
    );
}

} // namespace Arctic::Renderer
