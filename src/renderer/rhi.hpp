#pragma once

#include <array>
#include <functional>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <SDL3/SDL_video.h>

#pragma warning(push)
#pragma warning(disable : 4100)
#include "tracy/TracyD3D12.hpp"
#pragma warning(pop)

#include "compiler.hpp"
#include "comptr.hpp"

namespace Arctic::Renderer
{

class RHI
{
  public:
    static constexpr size_t NUM_FRAMES = 3;

  private:
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12CommandQueue> m_command_queue;

    tracy::D3D12QueueCtx *m_tracy_d3d12_ctx;

    bool m_allow_tearing{false};
    ComPtr<IDXGISwapChain4> m_swapchain;
    DXGI_FORMAT m_swapchain_format{DXGI_FORMAT_R8G8B8A8_UINT};
    std::array<ComPtr<ID3D12Resource>, NUM_FRAMES> m_backbuffers;

    ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
    UINT m_rtv_descriptor_size{0};

    std::array<ComPtr<ID3D12CommandAllocator>, NUM_FRAMES> m_command_allocators;
    ComPtr<ID3D12GraphicsCommandList> m_command_list;
    UINT m_current_backbuffer_index{0};

    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fence_value{0};
    std::array<uint64_t, NUM_FRAMES> m_frame_fence_values{};
    HANDLE m_fence_event{nullptr};

    struct
    {
        ComPtr<ID3D12CommandAllocator> command_allocator;
        ComPtr<ID3D12GraphicsCommandList> command_list;
        ComPtr<ID3D12Fence> fence;
        HANDLE fence_event;
        uint64_t fence_value{0};
    } m_immediate_submit;

    Compiler m_compiler;

    RHI(const RHI &) = delete;
    RHI &operator=(const RHI &) = delete;
    RHI(RHI &&) = delete;
    RHI &operator=(RHI &&) = delete;

  public:
    RHI() = default;

    ~RHI()
    {
        TracyD3D12Destroy(m_tracy_d3d12_ctx);
    }

    [[nodiscard]] bool init(SDL_Window *window, uint64_t width, uint32_t height);

    [[nodiscard]] bool resize(uint32_t new_width, int32_t new_height);

    [[nodiscard]] bool
    render_frame(std::function<
                 void(ID3D12GraphicsCommandList *, ID3D12Resource *, D3D12_CPU_DESCRIPTOR_HANDLE)>
                     &&render_func);

    [[nodiscard]] ID3D12Device2 *device()
    {
        return m_device.Get();
    }

    [[nodiscard]] tracy::D3D12QueueCtx *tracy_ctx()
    {
        return m_tracy_d3d12_ctx;
    }

    [[nodiscard]] DXGI_FORMAT swapchain_format()
    {
        return m_swapchain_format;
    }

    [[nodiscard]] const Compiler &compiler() const
    {
        return m_compiler;
    }

    [[nodiscard]] bool create_descriptor_heap(
        D3D12_DESCRIPTOR_HEAP_TYPE type, UINT num_descriptors, D3D12_DESCRIPTOR_HEAP_FLAGS flags,
        ComPtr<ID3D12DescriptorHeap> &out_heap
    );

    [[nodiscard]] bool update_render_target_views();

    [[nodiscard]] bool immediate_submit(std::function<void(ID3D12GraphicsCommandList *cmd_list)> &&f
    );

    [[nodiscard]] bool create_buffer(
        uint64_t size, D3D12_RESOURCE_STATES initial_state, D3D12_HEAP_TYPE heap_type,
        ComPtr<ID3D12Resource> &out_buffer
    );

    [[nodiscard]] bool create_texture(
        uint64_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_STATES initial_state,
        ComPtr<ID3D12Resource> &out_texture, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
    );

    [[nodiscard]] bool upload_to_buffer(
        ID3D12Resource *dst_buffer, D3D12_RESOURCE_STATES dst_buffer_state, void *src_data,
        uint64_t src_data_size
    );

    [[nodiscard]] bool upload_to_texture(
        ID3D12Resource *dst_texture, D3D12_RESOURCE_STATES dst_texture_state, void *src_data,
        uint64_t width, uint64_t height, uint64_t channels
    );

    [[nodiscard]] bool
    signal_fence(ID3D12Fence *fence, uint64_t &fence_value, uint64_t &out_wait_value);
    [[nodiscard]] bool wait_for_fence_value(ID3D12Fence *fence, HANDLE fence_event, uint64_t value);
    [[nodiscard]] bool flush();
};

} // namespace Arctic::Renderer
