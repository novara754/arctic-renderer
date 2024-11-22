#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"

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

    struct
    {
        uint32_t width, height;
    } m_output_size{};

    DXGI_FORMAT m_output_format{};
    ComPtr<ID3D12Resource> m_output_texture;

    ComPtr<ID3D12DescriptorHeap> m_uav_heap;
    uint32_t m_uav_descriptor_size{};

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    PostProcessPass() = delete;
    PostProcessPass(const PostProcessPass &) = delete;
    PostProcessPass &operator=(const PostProcessPass &) = delete;
    PostProcessPass(PostProcessPass &&) = delete;
    PostProcessPass &operator=(PostProcessPass &&) = delete;

  public:
    explicit PostProcessPass(RHI *engine) : m_rhi(engine)
    {
    }

    [[nodiscard]] ID3D12Resource *output_texture() const
    {
        return m_output_texture.Get();
    }

    [[nodiscard]] bool
    init(uint32_t width, uint32_t height, ID3D12Resource *input, DXGI_FORMAT output_format);

    [[nodiscard]] bool resize(uint32_t new_width, uint32_t new_height);

    void run(ID3D12GraphicsCommandList *cmd_list, uint32_t tm_method, float gamma, float exposure);
};
