#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "rhi.hpp"
#include "scene.hpp"

class ForwardPass
{
    struct ConstantBuffer
    {
        glm::mat4 model;
        glm::mat4 proj_view;
        glm::mat4 light_proj_view;

        glm::vec3 sun_dir;
        float ambient;
        glm::vec3 sun_color;

        uint32_t padding0{0};
    };

    static_assert(
        sizeof(ConstantBuffer) % 4 == 0,
        "Size of ForwardPass::ConstantBuffer is not a multiple of 4"
    );

    RHI *m_rhi;

    struct
    {
        uint32_t width, height;
    } m_output_size{};

    ComPtr<ID3D12DescriptorHeap> m_rtv_heap;

    ComPtr<ID3D12DescriptorHeap> m_srv_heap;
    UINT m_srv_descriptor_size{0};

    ComPtr<ID3D12DescriptorHeap> m_dsv_heap;
    UINT m_dsv_descriptor_size{0};

    ComPtr<ID3D12Resource> m_color_target;
    ComPtr<ID3D12Resource> m_depth_texture;
    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    ForwardPass() = delete;
    ForwardPass(const ForwardPass &) = delete;
    ForwardPass &operator=(const ForwardPass &) = delete;
    ForwardPass(ForwardPass &&) = delete;
    ForwardPass &operator=(ForwardPass &&) = delete;

  public:
    explicit ForwardPass(RHI *engine) : m_rhi(engine)
    {
    }

    [[nodiscard]] ID3D12Resource *color_target()
    {
        return m_color_target.Get();
    }

    [[nodiscard]] bool init(uint32_t width, uint32_t height, ID3D12Resource *shadow_map);

    [[nodiscard]] bool resize(uint32_t new_width, uint32_t new_height);

    void run(ID3D12GraphicsCommandList *cmd_list, const Scene &scene);

    void create_srv_tex2d(int32_t index, ID3D12Resource *resource, DXGI_FORMAT format);
};
