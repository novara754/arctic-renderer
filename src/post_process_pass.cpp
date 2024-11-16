#include "post_process_pass.hpp"

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "dxerr.hpp"

#define CONSTANTS_SIZE(ty) ((sizeof(ty) + 3) / 4)

bool PostProcessPass::init(uint32_t width, uint32_t height, ID3D12Resource *target)
{
    m_output_size = {width, height};

    if (!m_engine->create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            1,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            m_uav_heap
        ))
    {
        spdlog::error("PostProcessPass::init: failed to create uav heap");
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
    uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;
    m_engine->device()->CreateUnorderedAccessView(
        target,
        nullptr,
        &uav_desc,
        m_uav_heap->GetCPUDescriptorHandleForHeapStart()
    );

    ComPtr<ID3DBlob> cs_code;
    if (!compile_shader(L"../shaders/post_process.hlsl", "main", "cs_5_0", &cs_code))
    {
        spdlog::error("PostProcessPass::init: failed to compile shader");
        return false;
    }
    spdlog::trace("PostProcessPass::init: compiled shader");

    ComPtr<ID3DBlob> root_signature;

    std::array<CD3DX12_DESCRIPTOR_RANGE, 1> ranges{};
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    std::array<CD3DX12_ROOT_PARAMETER, 2> root_parameters{};
    root_parameters[0].InitAsConstants(CONSTANTS_SIZE(ConstantBuffer), 0);
    root_parameters[1].InitAsDescriptorTable(static_cast<UINT>(ranges.size()), ranges.data());

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc
        .Init(static_cast<UINT>(root_parameters.size()), root_parameters.data(), 0, nullptr);
    DXERR(
        D3D12SerializeRootSignature(
            &root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &root_signature,
            nullptr
        ),
        "PostProcessPass::init: failed to serialize root signature"
    );
    DXERR(
        m_engine->device()->CreateRootSignature(
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
        m_engine->device()->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&m_pipeline)),
        "PostProcessPass::init: failed to create pipeline state"
    );

    return true;
}

[[nodiscard]] bool PostProcessPass::resize(uint32_t new_width, uint32_t new_height)
{
    m_output_size = {new_width, new_height};
    return true;
}

void PostProcessPass::run(ID3D12GraphicsCommandList *cmd_list, float gamma)
{
    ConstantBuffer constants{.gamma = gamma};

    std::array heaps{m_uav_heap.Get()};
    D3D12_GPU_DESCRIPTOR_HANDLE uav_handle = m_uav_heap->GetGPUDescriptorHandleForHeapStart();

    cmd_list->SetComputeRootSignature(m_root_signature.Get());
    cmd_list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
    cmd_list->SetPipelineState(m_pipeline.Get());
    cmd_list->SetComputeRoot32BitConstants(0, CONSTANTS_SIZE(ConstantBuffer), &constants, 0);
    cmd_list->SetComputeRootDescriptorTable(1, uav_handle);
    cmd_list->Dispatch(
        (m_output_size.width + GROUP_WIDTH - 1) / GROUP_WIDTH,
        (m_output_size.height + GROUP_HEIGHT - 1) / GROUP_HEIGHT,
        1
    );
}
