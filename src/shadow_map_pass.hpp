#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "engine.hpp"
#include "scene.hpp"

class ShadowMapPass
{
    struct ConstantBuffer
    {
        DirectX::XMMATRIX view;
        DirectX::XMMATRIX proj;
    };

    static constexpr uint32_t SIZE = 2000;

    Engine *m_engine;

    ComPtr<ID3D12DescriptorHeap> m_dsv_heap;
    ComPtr<ID3D12Resource> m_depth_texture;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    ShadowMapPass() = delete;
    ShadowMapPass(const ShadowMapPass &) = delete;
    ShadowMapPass &operator=(const ShadowMapPass &) = delete;
    ShadowMapPass(ShadowMapPass &&) = delete;
    ShadowMapPass &operator=(ShadowMapPass &&) = delete;

  public:
    explicit ShadowMapPass(Engine *engine) : m_engine(engine)
    {
    }

    [[nodiscard]] ID3D12Resource *shadow_map()
    {
        return m_depth_texture.Get();
    }

    [[nodiscard]] bool init();

    void run(ID3D12GraphicsCommandList *cmd_list, Scene &scene);
};
