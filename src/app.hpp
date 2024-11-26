#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "renderer.hpp"
#include "scene.hpp"

class App
{
  public:
    static constexpr uint32_t WINDOW_WIDTH = 1280;
    static constexpr uint32_t WINDOW_HEIGHT = 720;

  private:
    static constexpr int FRAME_TIME_HISTORY_SIZE = 1000;

    Renderer m_renderer;

    std::chrono::high_resolution_clock::time_point m_last_frame_time;
    float m_delta_time;
    std::deque<float> m_frame_time_history;
    bool m_show_fps_graph{false};

    struct
    {
        bool w, a, s, d, space, ctrl, rmb;
    } m_input{};
    float m_camera_speed{10.0f};
    float m_mouse_sensitivity{0.5f};

    std::filesystem::path m_scene_path;
    bool m_update_lights{true};
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
        .point_lights{
            PointLight{
                .position = {0.0f, 1.0f, 0.0f},
                .color = {10.0f, 0.0f, 0.0f},
            },
        },
        .meshes{},
        .objects{},
    };
    Settings m_settings;

  public:
    explicit App(SDL_Window *window, const std::filesystem::path &scene_path)
        : m_renderer(window, WINDOW_WIDTH, WINDOW_HEIGHT), m_scene_path(scene_path)
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
