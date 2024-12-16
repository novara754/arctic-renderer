#pragma once

#include <span>

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"
#include "scene.hpp"

namespace Arctic::Renderer
{

class ShadowMapPass
{
    struct ConstantBuffer
    {
        glm::mat4 model;
        glm::mat4 proj_view;
    };

  public:
    static constexpr uint32_t SIZE = 4000;

    struct RunData
    {
        D3D12_CPU_DESCRIPTOR_HANDLE shadow_map_dsv;
        std::span<Mesh> meshes;
        const Scene &scene;
    };

  private:
    RHI *m_rhi;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    ShadowMapPass() = delete;
    ShadowMapPass(const ShadowMapPass &) = delete;
    ShadowMapPass &operator=(const ShadowMapPass &) = delete;
    ShadowMapPass(ShadowMapPass &&) = delete;
    ShadowMapPass &operator=(ShadowMapPass &&) = delete;

  public:
    explicit ShadowMapPass(RHI *rhi) : m_rhi(rhi)
    {
    }

    [[nodiscard]] bool init();

    void run(ID3D12GraphicsCommandList *cmd_list, const RunData &run_data);
};

} // namespace Arctic::Renderer
