#pragma once

#include <array>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl.h>

// clang-format off
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
// clang-format on

#include <directx/d3dx12.h>

#include <SDL3/SDL_video.h>

#include <spdlog/spdlog.h>

#define DXERR(x, msg)                                                                              \
    if (FAILED(x))                                                                                 \
    {                                                                                              \
        spdlog::error("{}: {}", msg, x);                                                           \
        return false;                                                                              \
    }

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

class App
{
    static constexpr size_t NUM_FRAMES = 2;

  public:
    static constexpr int WINDOW_WIDTH = 1280;
    static constexpr int WINDOW_HEIGHT = 720;

  private:
    SDL_Window *m_window;

    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12CommandQueue> m_command_queue;
    bool m_allow_tearing;
    ComPtr<IDXGISwapChain4> m_swapchain;
    std::array<ComPtr<ID3D12Resource>, NUM_FRAMES> m_backbuffers;
    ComPtr<ID3D12DescriptorHeap> m_rtv_descriptor_heap;
    UINT m_rtv_descriptor_size;
    std::array<ComPtr<ID3D12CommandAllocator>, NUM_FRAMES> m_command_allocators;

    ComPtr<ID3D12GraphicsCommandList> m_command_list;
    UINT m_current_backbuffer_index;

    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fence_value{0};
    std::array<uint64_t, NUM_FRAMES> m_frame_fence_values{};
    HANDLE m_fence_event;

  public:
    explicit App(SDL_Window *window) : m_window(window)
    {
    }

    [[nodiscard]] bool init();

    void run();

  private:
    [[nodiscard]] bool render_frame();

    [[nodiscard]] bool create_descriptor_heap(
        D3D12_DESCRIPTOR_HEAP_TYPE type, UINT num_descriptors,
        ComPtr<ID3D12DescriptorHeap> &out_heap
    );

    [[nodiscard]] bool signal_fence(uint64_t &out_value);
    [[nodiscard]] bool wait_for_fence_value(uint64_t value);
    [[nodiscard]] bool flush();
};
