#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"

namespace Arctic::Renderer
{

class PostProcessPass
{
    struct ConstantBuffer
    {
        float gamma;
        uint32_t tm_method;
        float exposure;
    };

    static constexpr uint32_t GROUP_WIDTH = 16;
    static constexpr uint32_t GROUP_HEIGHT = 16;

    RHI *m_rhi;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    PostProcessPass() = delete;
    PostProcessPass(const PostProcessPass &) = delete;
    PostProcessPass &operator=(const PostProcessPass &) = delete;
    PostProcessPass(PostProcessPass &&) = delete;
    PostProcessPass &operator=(PostProcessPass &&) = delete;

  public:
    explicit PostProcessPass(RHI *rhi) : m_rhi(rhi)
    {
    }

    [[nodiscard]] bool init();

    void
    run(ID3D12GraphicsCommandList *cmd_list, D3D12_GPU_DESCRIPTOR_HANDLE descriptor_handle,
        uint32_t width, uint32_t height, uint32_t tm_method, float gamma, float exposure);
};

} // namespace Arctic::Renderer
