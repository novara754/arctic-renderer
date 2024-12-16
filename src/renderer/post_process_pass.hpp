#pragma once

#include <span>

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"

namespace Arctic::Renderer
{

class PostProcessPass
{
    struct ConstantBuffer
    {
        uint32_t input_idx;
        uint32_t output_idx;

        float gamma;
        uint32_t tm_method;
        float exposure;
    };

  public:
    struct RunData
    {
        uint32_t input_uav_idx;
        uint32_t output_uav_idx;
        uint32_t viewport_width;
        uint32_t viewport_height;

        uint32_t tm_method;
        float gamma;
        float exposure;
    };

  private:
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

    void run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data);
};

} // namespace Arctic::Renderer
