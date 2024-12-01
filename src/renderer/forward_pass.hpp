#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"
#include "scene.hpp"

namespace Arctic::Renderer
{

class ForwardPass
{
    struct ConstantBuffer
    {
        glm::vec3 eye;
        uint32_t padding0{0};
        glm::mat4 model;
        glm::mat4 proj_view;
        glm::mat4 light_proj_view;

        glm::vec3 sun_dir;
        float ambient;
        glm::vec3 sun_color;

        uint32_t padding1{0};
    };

    static_assert(
        sizeof(ConstantBuffer) % 4 == 0,
        "Size of ForwardPass::ConstantBuffer is not a multiple of 4"
    );

    RHI *m_rhi;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    ForwardPass() = delete;
    ForwardPass(const ForwardPass &) = delete;
    ForwardPass &operator=(const ForwardPass &) = delete;
    ForwardPass(ForwardPass &&) = delete;
    ForwardPass &operator=(ForwardPass &&) = delete;

  public:
    explicit ForwardPass(RHI *rhi) : m_rhi(rhi)
    {
    }

    [[nodiscard]] bool init();

    void
    run(ID3D12GraphicsCommandList *cmd_list, D3D12_CPU_DESCRIPTOR_HANDLE color_target_rtv,
        D3D12_CPU_DESCRIPTOR_HANDLE depth_target_dsv, D3D12_GPU_DESCRIPTOR_HANDLE srv_base_handle,
        uint32_t srv_descriptor_size, uint32_t width, uint32_t height, const Scene &scene);
};

} // namespace Arctic::Renderer