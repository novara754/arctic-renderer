#pragma once

#include <array>
#include <chrono>
#include <filesystem>

#include <d3d12.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "engine.hpp"
#include "forward_pass.hpp"
#include "post_process_pass.hpp"
#include "scene.hpp"
#include "shadow_map_pass.hpp"

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

    std::chrono::high_resolution_clock::time_point m_last_frame_time;
    float m_delta_time;

    struct
    {
        bool w, a, s, d, space, ctrl, rmb;
    } m_input{};
    float m_camera_speed{10.0f};
    float m_mouse_sensitivity{0.5f};

    Engine m_engine;
    ShadowMapPass m_shadow_map_pass;
    ForwardPass m_forward_pass;
    PostProcessPass m_post_process_pass;

    ComPtr<ID3D12DescriptorHeap> m_imgui_cbv_srv_heap;

    std::filesystem::path m_scene_path;
    Scene m_scene{
        .camera{
            .eye = {0.0f, 5.0f, 0.0f},
            .rotation = {0.0f, 0.0f},
            .aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT),
            .fov_y = 45.0f,
            .z_near_far = {0.1f, 1000.0f},
        },
        .ambient = 0.1f,
        .sun{
            .position = {-10.0f, 32.0f, -2.48f},
            .rotation = {-70.0f, 12.0f},
            .color = {8.0f, 8.0f, 8.0f},
        },
        .meshes{},
        .objects{},
    };
    float m_gamma{2.2f};
    int m_tm_method{0};
    float m_exposure{1.0f};

  public:
    explicit App(SDL_Window *window, const std::filesystem::path &scene_path)
        : m_window(window), m_shadow_map_pass(&m_engine), m_forward_pass(&m_engine),
          m_post_process_pass(&m_engine), m_scene_path(scene_path)
    {
    }

    [[nodiscard]] bool init();

    void run();

  private:
    void handle_event(SDL_Event &event);

    void update();

    [[nodiscard]] bool load_scene(const std::filesystem::path &path, Scene &out_scene);

    [[nodiscard]] bool render_frame();

    void build_ui();

    [[nodiscard]] bool handle_resize();
};
