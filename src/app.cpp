#include "app.hpp"

#include <chrono>
#include <cstddef>
#include <tuple>

#include <spdlog/spdlog.h>

#include <SDL3/SDL_events.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "implot.h"

#include "stb_image.h"

glm::mat4 assimp_to_mat4(const aiMatrix4x4 &mat);

[[nodiscard]] bool App::init()
{
    if (!m_renderer.init())
    {
        spdlog::error("App::init: failed to initialize renderer");
        return false;
    }

    if (!load_scene(m_scene_path, m_scene))
    {
        spdlog::error("App::init: failed to load scene");
        return false;
    }

    return true;
}

void App::run()
{
    spdlog::trace("App::run: entering loop");
    m_last_frame_time = std::chrono::high_resolution_clock::now();
    while (true)
    {
        std::chrono::high_resolution_clock::time_point now =
            std::chrono::high_resolution_clock::now();
        m_delta_time =
            std::chrono::duration<float, std::milli>(now - m_last_frame_time).count() / 1000.0f;
        m_last_frame_time = now;

        if (m_delta_time > 0.0f)
        {
            m_frame_time_history.emplace_back(m_delta_time);
        }
        if (m_frame_time_history.size() > FRAME_TIME_HISTORY_SIZE)
        {
            m_frame_time_history.pop_front();
        }

        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                break;
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                if (!handle_resize())
                {
                    spdlog::error("App::run: resize was requested but failed");
                    break;
                };
            }

            handle_event(event);

            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        update();

        if (!render_frame())
        {
            spdlog::error("App::run: render_frame failed");
            break;
        }
    }
    spdlog::trace("App::run: exited loop");

    spdlog::trace("App::run: flushing...");
    if (!m_renderer.flush())
    {
        spdlog::error("App::run: flush failed");
    }

    m_renderer.cleanup();
}

void App::handle_event(SDL_Event &event)
{
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        bool pressed = event.type == SDL_EVENT_KEY_DOWN;
        switch (event.key.scancode)
        {
            case SDL_SCANCODE_W:
                m_input.w = pressed;
                break;
            case SDL_SCANCODE_A:
                m_input.a = pressed;
                break;
            case SDL_SCANCODE_S:
                m_input.s = pressed;
                break;
            case SDL_SCANCODE_D:
                m_input.d = pressed;
                break;
            case SDL_SCANCODE_SPACE:
                m_input.space = pressed;
                break;
            case SDL_SCANCODE_LCTRL:
                m_input.ctrl = pressed;
                break;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == 3)
        {
            m_input.rmb = event.button.down;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION && m_input.rmb)
    {
        m_scene.camera.rotation.y += event.motion.xrel * m_mouse_sensitivity;
        m_scene.camera.rotation.x -= event.motion.yrel * m_mouse_sensitivity;
    }
}

void App::update()
{
    float fwd_input = static_cast<float>(m_input.w) - static_cast<float>(m_input.s);
    float right_input = static_cast<float>(m_input.d) - static_cast<float>(m_input.a);
    float up_input = static_cast<float>(m_input.space) - static_cast<float>(m_input.ctrl);

    glm::vec3 forward = m_scene.camera.forward();
    glm::vec3 up = m_scene.camera.up();
    glm::vec3 right = glm::cross(forward, up);

    glm::vec3 &eye = m_scene.camera.eye;
    eye += m_camera_speed * m_delta_time * fwd_input * forward;
    eye += m_camera_speed * m_delta_time * up_input * up;
    eye += m_camera_speed * m_delta_time * right_input * right;

    if (m_update_lights)
    {
        spdlog::debug("App::update: updating lights buffer");
        m_renderer.update_lights(m_scene.point_lights);
        m_update_lights = false;
    }
}

bool App::load_scene(const std::filesystem::path &path, Scene &out_scene)
{
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs |
            aiProcess_CalcTangentSpace
    );
    if (scene == nullptr)
    {
        spdlog::error("App::load_scene: failed to load file");
        return false;
    }

    if (scene->mRootNode == nullptr)
    {
        spdlog::error("App::load_scene: file has no root node");
        return false;
    }

    for (size_t mat_idx = 0; mat_idx < scene->mNumMaterials; ++mat_idx)
    {

        std::filesystem::path diffuse_path = path;
        std::filesystem::path normal_path = path;
        std::filesystem::path metalness_roughness_path = path;

        const aiMaterial *ai_material = scene->mMaterials[mat_idx];
        if (ai_material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            aiString diffuse_name;
            ai_material->GetTexture(aiTextureType_DIFFUSE, 0, &diffuse_name);
            diffuse_path.replace_filename(diffuse_name.C_Str());
        }
        else
        {
            spdlog::warn(
                "App::load_scene: material #{} missing diffuse texture, using fallback",
                mat_idx
            );
            diffuse_path = "./assets/white.png";
        }

        if (ai_material->GetTextureCount(aiTextureType_NORMALS) > 0)
        {
            aiString normal_name;
            ai_material->GetTexture(aiTextureType_NORMALS, 0, &normal_name);
            normal_path.replace_filename(normal_name.C_Str());
        }
        else
        {
            spdlog::warn(
                "App::load_scene: material #{} missing normal texture, using fallback",
                mat_idx
            );
            normal_path = "./assets/normal.png";
        }

        if (ai_material->GetTextureCount(aiTextureType_METALNESS) > 0)
        {
            aiString metalness_roughness_name;
            ai_material->GetTexture(aiTextureType_METALNESS, 0, &metalness_roughness_name);
            metalness_roughness_path.replace_filename(metalness_roughness_name.C_Str());
        }
        else
        {
            spdlog::warn(
                "App::load_scene: material #{} missing metalness/roughness texture, using fallback",
                mat_idx
            );
            metalness_roughness_path = "./assets/white.png";
        }

        int diffuse_width, diffuse_height;
        uint8_t *diffuse_image_data =
            stbi_load(diffuse_path.string().c_str(), &diffuse_width, &diffuse_height, nullptr, 4);
        if (!diffuse_image_data)
        {
            spdlog::error("App::load_scene: failed to load image file `{}`", diffuse_path.string());
            return false;
        }

        int normal_width, normal_height;
        uint8_t *normal_image_data =
            stbi_load(normal_path.string().c_str(), &normal_width, &normal_height, nullptr, 4);
        if (!normal_image_data)
        {
            spdlog::error("App::load_scene: failed to load image file `{}`", normal_path.string());
            return false;
        }

        int metalness_roughness_width, metalness_roughness_height;
        uint8_t *metalness_roughness_image_data = stbi_load(
            metalness_roughness_path.string().c_str(),
            &metalness_roughness_width,
            &metalness_roughness_height,
            nullptr,
            4
        );
        if (!metalness_roughness_image_data)
        {
            spdlog::error(
                "App::load_scene: failed to load image file `{}`",
                metalness_roughness_path.string()
            );
            return false;
        }

        Material material;
        if (!m_renderer.create_material(
                material,
                diffuse_image_data,
                diffuse_width,
                diffuse_height,
                normal_image_data,
                normal_width,
                normal_height,
                metalness_roughness_image_data,
                metalness_roughness_width,
                metalness_roughness_height
            ))
        {
            spdlog::error("App::load_scene: failed to create material #{}", mat_idx);
            return false;
        }

        out_scene.materials.emplace_back(material);
    }

    for (size_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; ++mesh_idx)
    {
        const aiMesh *ai_mesh = scene->mMeshes[mesh_idx];

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (size_t vertex_idx = 0; vertex_idx < ai_mesh->mNumVertices; ++vertex_idx)
        {
            Vertex vertex{
                .position =
                    {
                        ai_mesh->mVertices[vertex_idx].x,
                        ai_mesh->mVertices[vertex_idx].y,
                        ai_mesh->mVertices[vertex_idx].z,
                    },
                .normal =
                    {
                        ai_mesh->mNormals[vertex_idx].x,
                        ai_mesh->mNormals[vertex_idx].y,
                        ai_mesh->mNormals[vertex_idx].z,
                    },
                .tangent =
                    {
                        ai_mesh->mTangents[vertex_idx].x,
                        ai_mesh->mTangents[vertex_idx].y,
                        ai_mesh->mTangents[vertex_idx].z,
                    },
                .bitangent =
                    {
                        ai_mesh->mBitangents[vertex_idx].x,
                        ai_mesh->mBitangents[vertex_idx].y,
                        ai_mesh->mBitangents[vertex_idx].z,
                    },
                .tex_coords =
                    {
                        ai_mesh->mTextureCoords[0][vertex_idx].x,
                        ai_mesh->mTextureCoords[0][vertex_idx].y,
                    },
            };
            vertices.emplace_back(vertex);
        }

        for (size_t face_idx = 0; face_idx < ai_mesh->mNumFaces; ++face_idx)
        {
            const aiFace &face = ai_mesh->mFaces[face_idx];
            for (size_t index_idx = 0; index_idx < face.mNumIndices; ++index_idx)
            {
                indices.emplace_back(static_cast<uint32_t>(face.mIndices[index_idx]));
            }
        }

        Mesh mesh;
        if (!m_renderer.create_mesh(mesh, vertices, indices, ai_mesh->mMaterialIndex))
        {
            spdlog::error("App::load_scene: failed to create mesh #{}", mesh_idx);
            return false;
        }

        out_scene.meshes.emplace_back(mesh);
    }

    std::vector nodes_to_process{
        std::make_pair(scene->mRootNode, glm::mat4(1.0f)),
    };
    while (!nodes_to_process.empty())
    {
        const aiNode *node = nodes_to_process.back().first;
        glm::mat4 parent_trs = nodes_to_process.back().second;
        nodes_to_process.pop_back();

        glm::mat4 this_trs = assimp_to_mat4(node->mTransformation);
        glm::mat4 trs = parent_trs * this_trs;

        for (size_t i = 0; i < node->mNumChildren; ++i)
        {
            nodes_to_process.emplace_back(std::make_pair(node->mChildren[i], trs));
        }

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            out_scene.objects.emplace_back(Object{
                .trs = trs,
                .mesh_idx = node->mMeshes[i],
            });
        }
    }

    return true;
}

bool App::render_frame()
{
    if (!m_renderer.render_frame(m_scene, m_settings, [this] { build_ui(); }))
    {
        spdlog::error("App::render_frame: failed");
        return false;
    }

    return true;
}

void App::build_ui()
{
    if (ImGui::Begin("Stats"))
    {
        ImGui::Text("Frame Time: %.2f ms", m_delta_time * 1000.0f);
        ImGui::Text("FPS: %u", static_cast<uint32_t>(1.0f / m_delta_time));

        ImGui::Checkbox("Show FPS graph", &m_show_fps_graph);

        if (ImPlot::BeginPlot("FPS"))
        {
            ImPlot::SetupAxis(ImAxis_X1, nullptr);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, static_cast<double>(FRAME_TIME_HISTORY_SIZE));

            ImPlot::SetupAxis(ImAxis_Y1, "ms");
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 10.0);

            ImPlot::SetupAxis(ImAxis_Y2, "FPS", ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 1500.0);

            ImPlot::SetAxis(ImAxis_Y1);
            ImPlot::PlotLineG(
                "Frame Time",
                [](int i, void *data) {
                    const auto &history = *static_cast<std::deque<float> *>(data);
                    float y = history[i] * 1000.0f;
                    return ImPlotPoint(static_cast<double>(i), static_cast<double>(y));
                },
                &m_frame_time_history,
                static_cast<int>(m_frame_time_history.size())
            );

            if (m_show_fps_graph)
            {
                ImPlot::SetAxis(ImAxis_Y2);
                ImPlot::PlotLineG(
                    "FPS",
                    [](int i, void *data) {
                        const auto &history = *static_cast<std::deque<float> *>(data);
                        float y = 1.0f / history[i];
                        return ImPlotPoint(static_cast<double>(i), static_cast<double>(y));
                    },
                    &m_frame_time_history,
                    static_cast<int>(m_frame_time_history.size())
                );
            }

            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Camera");
        ImGui::SliderFloat("Speed", &m_camera_speed, 0.1f, 5000.0f);
        ImGui::SliderFloat("Sensitivity", &m_mouse_sensitivity, 0.01f, 2.0f);
        ImGui::DragFloat3("Position", glm::value_ptr(m_scene.camera.eye), 0.1f);
        ImGui::DragFloat2(
            "Rotation",
            glm::value_ptr(m_scene.camera.rotation),
            0.1f,
            -360.0f,
            360.0f
        );
        ImGui::DragFloat2("Z Near/Far", m_scene.camera.z_near_far.data(), 0.01f, 0.001f, 10000.0f);

        ImGui::SeparatorText("Light");
        ImGui::SliderFloat("Ambient", &m_scene.ambient, 0.0f, 1.0f);
        ImGui::DragFloat3("Sun Position", glm::value_ptr(m_scene.sun.position));
        ImGui::DragFloat2(
            "Sun Rotation",
            glm::value_ptr(m_scene.sun.rotation),
            0.1f,
            -360.0f,
            360.0f
        );
        ImGui::ColorEdit3(
            "Sun Color",
            glm::value_ptr(m_scene.sun.color),
            ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel
        );

        ImGui::SeparatorText("Post Processing");
        ImGui::DragFloat("Gamma", &m_settings.gamma, 0.01f, 0.1f, 5.0f);
        ImGui::Combo("Tone Mapping", &m_settings.tm_method, "Reinhard\0Exposure\0ACES\0");
        if (m_settings.tm_method == 1)
        {
            ImGui::DragFloat("Exposure", &m_settings.exposure, 0.1f, 0.0f, 10.0f);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Lights", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        for (PointLight &light : m_scene.point_lights)
        {
            ImGui::PushID(&light);
            ImGui::Separator();
            m_update_lights |= ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
            m_update_lights |= ImGui::ColorEdit3(
                "Color",
                glm::value_ptr(light.color),
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float |
                    ImGuiColorEditFlags_PickerHueWheel
            );
            ImGui::PopID();
        }

        if (m_scene.point_lights.size() < Renderer::MAX_NUM_POINT_LIGHTS)
        {
            if (ImGui::Button("Add"))
            {
                m_scene.point_lights.emplace_back(PointLight{
                    .position{0.0f, 0.0f, 0.0f},
                    .color{10.0f, 10.0f, 10.0f},
                });
                m_update_lights = true;
            }
        }
    }
    ImGui::End();
}

bool App::handle_resize()
{
    uint32_t new_width, new_height;
    if (!m_renderer.resize(new_width, new_height))
    {
        spdlog::error("App::handle_resize: failed");
        return false;
    }

    m_scene.camera.aspect = static_cast<float>(new_width) / static_cast<float>(new_height);

    return true;
}

glm::mat4 assimp_to_mat4(const aiMatrix4x4 &mat)
{
    glm::mat4 out(
        mat.a1,
        mat.a2,
        mat.a3,
        mat.a4,

        mat.b1,
        mat.b2,
        mat.b3,
        mat.b4,

        mat.c1,
        mat.c2,
        mat.c3,
        mat.c4,

        mat.d1,
        mat.d2,
        mat.d3,
        mat.d4
    );
    return out;
}
