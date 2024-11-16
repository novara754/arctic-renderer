#pragma once

#include <d3d12.h>

#include "comptr.hpp"
#include "engine.hpp"

class PostProcessPass
{
    struct ConstantBuffer
    {
        float gamma;
    };

    static constexpr uint32_t GROUP_WIDTH = 16;
    static constexpr uint32_t GROUP_HEIGHT = 16;

    Engine *m_engine;

    struct
    {
        uint32_t width, height;
    } m_output_size{};

    ComPtr<ID3D12DescriptorHeap> m_uav_heap;

    ComPtr<ID3D12RootSignature> m_root_signature;
    ComPtr<ID3D12PipelineState> m_pipeline;

    PostProcessPass() = delete;
    PostProcessPass(const PostProcessPass &) = delete;
    PostProcessPass &operator=(const PostProcessPass &) = delete;
    PostProcessPass(PostProcessPass &&) = delete;
    PostProcessPass &operator=(PostProcessPass &&) = delete;

  public:
    explicit PostProcessPass(Engine *engine) : m_engine(engine)
    {
    }

    [[nodiscard]] bool init(uint32_t width, uint32_t height, ID3D12Resource *target);

    [[nodiscard]] bool resize(uint32_t new_width, uint32_t new_height);

    void run(ID3D12GraphicsCommandList *cmd_list, float gamma);
};
