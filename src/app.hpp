#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

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
        spdlog::error("{}: 0x{:x}", msg, x);                                                       \
        return false;                                                                              \
    }

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 tex_coords;
};

struct Mesh
{
    ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;

    ComPtr<ID3D12Resource> index_buffer;
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;

    uint32_t index_count;

    size_t material_idx;
};

struct Camera
{
    DirectX::XMFLOAT3 eye;
    DirectX::XMFLOAT2 rotation;
    float aspect;
    float fov_y;
    std::array<float, 2> z_near_far;

    [[nodiscard]] DirectX::XMMATRIX view_matrix() const
    {
        using namespace DirectX;

        XMFLOAT3 forward(
            XMScalarCos(XMConvertToRadians(this->rotation.x)) *
                XMScalarCos(XMConvertToRadians(this->rotation.y)),
            XMScalarSin(XMConvertToRadians(this->rotation.x)),
            XMScalarCos(XMConvertToRadians(this->rotation.x)) *
                XMScalarSin(XMConvertToRadians(this->rotation.y))
        );

        XMFLOAT3 up(0.0f, 1.0f, 0.0f);

        return XMMatrixLookToRH(
            XMLoadFloat3(&this->eye),
            XMLoadFloat3(&forward),
            XMLoadFloat3(&up)
        );
    }

    [[nodiscard]] DirectX::XMMATRIX proj_matrix() const
    {
        using namespace DirectX;

        return XMMatrixPerspectiveFovRH(
            XMConvertToRadians(this->fov_y),
            this->aspect,
            this->z_near_far[0],
            this->z_near_far[1]
        );
    }
};

struct Material
{
    ComPtr<ID3D12Resource> diffuse;
};

struct Scene
{
    Camera camera;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<size_t> objects;
};

class App
{
    static constexpr size_t NUM_FRAMES = 2;

  public:
    static constexpr int WINDOW_WIDTH = 1280;
    static constexpr int WINDOW_HEIGHT = 720;

  private:
    SDL_Window *m_window;

    struct
    {
        int width, height;
    } m_window_size{WINDOW_WIDTH, WINDOW_HEIGHT};

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

    struct
    {
        ComPtr<ID3D12CommandAllocator> command_allocator;
        ComPtr<ID3D12GraphicsCommandList> command_list;
        ComPtr<ID3D12Fence> fence;
        HANDLE fence_event;
        uint64_t fence_value{0};
    } m_immediate_submit;

    ComPtr<ID3D12DescriptorHeap> m_imgui_cbv_srv_heap;

    std::filesystem::path m_scene_path;
    Scene m_scene{
        .camera{
            .eye = {-8.0f, 5.0f, 0.0f},
            .rotation = {-20.0f, 0.0f},
            .aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
            .fov_y = 45.0f,
            .z_near_far = {0.1f, 1000.0f},
        },
        .meshes{},
        .objects{},
    };

    ComPtr<ID3D12DescriptorHeap> m_srv_heap;
    UINT m_srv_descriptor_size;

    ComPtr<ID3D12DescriptorHeap> m_dsv_descriptor_heap;
    UINT m_dsv_descriptor_size;
    ComPtr<ID3D12Resource> m_depth_texture;
    ComPtr<ID3D12RootSignature> m_forward_root_signature;
    ComPtr<ID3D12PipelineState> m_forward_pipeline;

    std::array<float, 3> m_background_color{1.0f, 0.5f, 0.1f};

  public:
    explicit App(SDL_Window *window, const std::filesystem::path &scene_path)
        : m_window(window), m_scene_path(scene_path)
    {
    }

    [[nodiscard]] bool init();

    void run();

  private:
    [[nodiscard]] bool load_scene(const std::filesystem::path &path, Scene &out_scene);

    [[nodiscard]] bool render_frame();

    void build_ui();

    [[nodiscard]] bool handle_resize();

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
        ComPtr<ID3D12Resource> &out_texture
    );

    [[nodiscard]] bool
    create_depth_texture(uint64_t width, uint32_t height, ComPtr<ID3D12Resource> &out_texture);

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
