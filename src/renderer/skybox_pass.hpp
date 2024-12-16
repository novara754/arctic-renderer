#pragma once

#include <d3d12.h>

#include <glm/mat4x4.hpp>

#include "comptr.hpp"
#include "rhi.hpp"
#include "scene.hpp"

namespace Arctic::Renderer
{

class SkyboxPass
{
    struct ConstantBuffer
    {
        uint32_t environment_idx;
        uint32_t padding0[3]{0};
        glm::mat4 proj_view;
    };

  public:
    struct RunData
    {
        D3D12_CPU_DESCRIPTOR_HANDLE color_target_rtv;
        D3D12_CPU_DESCRIPTOR_HANDLE depth_target_rtv;
        uint32_t environment_srv_idx;
        uint32_t viewport_width;
        uint32_t viewport_height;
        const Camera &camera;
    };

  private:
    RHI *m_rhi;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    SkyboxPass() = delete;
    SkyboxPass(const SkyboxPass &) = delete;
    SkyboxPass &operator=(const SkyboxPass &) = delete;
    SkyboxPass(SkyboxPass &&) = delete;
    SkyboxPass &operator=(SkyboxPass &&) = delete;

  public:
    explicit SkyboxPass(RHI *rhi) : m_rhi(rhi)
    {
    }

    [[nodiscard]] bool init();

    void run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data);
};

} // namespace Arctic::Renderer
