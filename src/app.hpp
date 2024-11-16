#pragma once

#include <array>
#include <filesystem>

#include <d3d12.h>

#include <SDL3/SDL_video.h>

#include "engine.hpp"
#include "forward_pass.hpp"
#include "scene.hpp"

class App
{
  public:
    static constexpr uint32_t WINDOW_WIDTH = 1280;
    static constexpr uint32_t WINDOW_HEIGHT = 720;

  private:
    SDL_Window *m_window;

    struct
    {
        uint32_t width, height;
    } m_window_size{WINDOW_WIDTH, WINDOW_HEIGHT};

    Engine m_engine;

    ForwardPass m_forward_pass;

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
        .ambient = 0.1f,
        .sun{
            .rotation = {-50.0f, 0.0f},
            .color = {1.0f, 1.0f, 1.0f},
        },
        .meshes{},
        .objects{},
    };

  public:
    explicit App(SDL_Window *window, const std::filesystem::path &scene_path)
        : m_window(window), m_forward_pass(&m_engine), m_scene_path(scene_path)
    {
    }

    [[nodiscard]] bool init();

    void run();

  private:
    [[nodiscard]] bool load_scene(const std::filesystem::path &path, Scene &out_scene);

    [[nodiscard]] bool render_frame();

    void build_ui();

    [[nodiscard]] bool handle_resize();
};
