#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>

#include <SDL3/SDL_video.h>

#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

#include "forward_pass.hpp"
#include "post_process_pass.hpp"
#include "scene.hpp"
#include "shadow_map_pass.hpp"

class Renderer
{
    SDL_Window *m_window;

    struct
    {
        uint32_t width, height;
    } m_window_size;

    RHI m_rhi;
    ShadowMapPass m_shadow_map_pass;
    ForwardPass m_forward_pass;
    PostProcessPass m_post_process_pass;

    ComPtr<ID3D12DescriptorHeap> m_imgui_cbv_srv_heap;

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

    [[nodiscard]] bool resize(uint32_t &out_width, uint32_t &out_height);

    [[nodiscard]] bool
    render_frame(const Scene &scene, const Settings &settings, std::function<void()> &&build_ui);

    [[nodiscard]] bool create_mesh(
        Mesh &out_mesh, std::span<Vertex> vertices, std::span<uint32_t> indices, size_t material_idx
    );

    [[nodiscard]] bool create_material(
        Material &out_material, size_t material_idx, void *diffuse_data, uint32_t diffuse_width,
        uint32_t diffuse_height
    );

    [[nodiscard]] bool flush()
    {
        return m_rhi.flush();
    }
};
