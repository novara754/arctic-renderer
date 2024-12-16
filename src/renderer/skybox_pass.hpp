#pragma once

// #include <cstdint>

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
        glm::mat4 proj_view;
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

    void
    run(ID3D12GraphicsCommandList *cmd_list, D3D12_CPU_DESCRIPTOR_HANDLE color_target_rtv,
        D3D12_CPU_DESCRIPTOR_HANDLE depth_target_rtv, D3D12_GPU_DESCRIPTOR_HANDLE environment_srv,
        uint32_t width, uint32_t height, const Camera &camera);
};

} // namespace Arctic::Renderer
