#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>

#include <SDL3/SDL_video.h>

#include "forward_pass.hpp"
#include "post_process_pass.hpp"
#include "scene.hpp"
#include "shadow_map_pass.hpp"

class Renderer
{
  public:
    static constexpr size_t MAX_NUM_POINT_LIGHTS = 16;

  private:
    struct LightsBuffer
    {
        uint32_t point_lights_len{0};
        uint32_t padding0[3]{};
        PointLight point_lights[Renderer::MAX_NUM_POINT_LIGHTS];
    };

    SDL_Window *m_window;

    struct
    {
        uint32_t width, height;
    } m_window_size;

    RHI m_rhi;

    ComPtr<ID3D12DescriptorHeap> m_imgui_cbv_srv_heap;

    ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
    uint32_t m_rtv_descriptor_size{0};
    uint32_t m_rtv_count{0};

    ComPtr<ID3D12DescriptorHeap> m_dsv_heap;
    uint32_t m_dsv_descriptor_size{0};
    uint32_t m_dsv_count{0};

    ComPtr<ID3D12DescriptorHeap> m_cbv_srv_uav_heap;
    uint32_t m_cbv_srv_uav_descriptor_size{0};
    uint32_t m_cbv_srv_uav_count{0};

    LightsBuffer m_lights_buffer_data;
    ComPtr<ID3D12Resource> m_lights_buffer;

    ComPtr<ID3D12Resource> m_sun_shadow_map;
    D3D12_CPU_DESCRIPTOR_HANDLE m_sun_shadow_map_dsv;

    ComPtr<ID3D12Resource> m_forward_color_target;
    D3D12_CPU_DESCRIPTOR_HANDLE m_forward_color_target_rtv;

    ComPtr<ID3D12Resource> m_forward_depth_target;
    D3D12_CPU_DESCRIPTOR_HANDLE m_forward_depth_target_dsv;

    ComPtr<ID3D12Resource> m_post_process_output;

    ShadowMapPass m_shadow_map_pass;

    ForwardPass m_forward_pass;
    D3D12_GPU_DESCRIPTOR_HANDLE m_forward_descriptors_base_handle;

    PostProcessPass m_post_process_pass;
    D3D12_GPU_DESCRIPTOR_HANDLE m_post_process_descriptors_base_handle;

    Renderer() = delete;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

  public:
    Renderer(SDL_Window *window, uint32_t initial_width, uint32_t initial_height)
        : m_window(window), m_window_size{initial_width, initial_height}, m_shadow_map_pass(&m_rhi),
          m_forward_pass(&m_rhi), m_post_process_pass(&m_rhi)
    {
    }

    [[nodiscard]] bool init();

    void cleanup();

    [[nodiscard]] bool resize(uint32_t &out_width, uint32_t &out_height);

    [[nodiscard]] bool
    render_frame(const Scene &scene, const Settings &settings, std::function<void()> &&build_ui);

    [[nodiscard]] bool create_mesh(
        Mesh &out_mesh, std::span<Vertex> vertices, std::span<uint32_t> indices, size_t material_idx
    );

    [[nodiscard]] bool create_material(
        Material &out_material, void *diffuse_data, uint32_t diffuse_width, uint32_t diffuse_height,
        void *normal_data, uint32_t normal_width, uint32_t normal_height,
        void *metalness_roughness_data, uint32_t metalness_roughness_width,
        uint32_t metalness_roughness_height
    );

    void update_lights(std::span<PointLight> point_lights);

    [[nodiscard]] bool flush()
    {
        return m_rhi.flush();
    }

  private:
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE
    create_rtv(ID3D12Resource *resource, DXGI_FORMAT format);

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE create_dsv(ID3D12Resource *resource);

    D3D12_GPU_DESCRIPTOR_HANDLE create_srv(ID3D12Resource *resource, DXGI_FORMAT format);

    D3D12_GPU_DESCRIPTOR_HANDLE create_uav(ID3D12Resource *resource, DXGI_FORMAT format);

    D3D12_GPU_DESCRIPTOR_HANDLE create_cbv(ID3D12Resource *resource);
};
