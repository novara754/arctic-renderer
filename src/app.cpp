#include "app.hpp"

#include <chrono>
#include <cstddef>
#include <tuple>

#include <spdlog/spdlog.h>

#include <SDL3/SDL_events.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

#include "stb_image.h"

glm::mat4 assimp_to_mat4(const aiMatrix4x4 &mat);

[[nodiscard]] bool App::init()
{
    if (!m_engine.init(m_window, m_window_size.width, m_window_size.height))
    {
        spdlog::error("App::init: failed to initialize engine");
        return false;
    }

    if (!m_shadow_map_pass.init())
    {
        spdlog::error("App::init: failed to initialize forward pass");
        return false;
    }

    if (!m_forward_pass
             .init(m_window_size.width, m_window_size.height, m_shadow_map_pass.shadow_map()))
    {
        spdlog::error("App::init: failed to initialize forward pass");
        return false;
    }

    if (!m_post_process_pass
             .init(m_window_size.width, m_window_size.height, m_forward_pass.color_target()))
    {
        spdlog::error("App::init: failed to initialize post process pass");
        return false;
    }

    // ------------
    // Initialize ImGui
    // -------
    {
        if (!m_engine.create_descriptor_heap(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                1,
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                m_imgui_cbv_srv_heap
            ))
        {
            spdlog::error("App::init: failed to create cbv srv heap for imgui");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;

        ImGui_ImplSDL3_InitForOther(m_window);
        ImGui_ImplDX12_Init(
            m_engine.device(),
            Engine::NUM_FRAMES,
            m_engine.swapchain_format(),
            m_imgui_cbv_srv_heap.Get(),
            m_imgui_cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(),
            m_imgui_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart()
        );
        spdlog::trace("App::init: initialized imgui");
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
    if (!m_engine.flush())
    {
        spdlog::error("App::run: flush failed");
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
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
}

bool App::load_scene(const std::filesystem::path &path, Scene &out_scene)
{
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs
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
        Material material;

        std::filesystem::path diffuse_path = path;

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
            diffuse_path = "../assets/white.png";
        }

        int width, height, channels = 4;
        uint8_t *image_data =
            stbi_load(diffuse_path.string().c_str(), &width, &height, nullptr, channels);
        if (!image_data)
        {
            spdlog::error("App::load_scene: failed to load image file `{}`", diffuse_path.string());
            return false;
        }

        bool res = m_engine.create_texture(
            width,
            height,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            material.diffuse
        );
        res &= m_engine.upload_to_texture(
            material.diffuse.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            image_data,
            width,
            height,
            channels
        );
        if (!res)
        {
            spdlog::error("App::load: failed to create diffuse texture for material #{}", mat_idx);
            return false;
        }

        m_forward_pass.create_srv_tex2d(
            static_cast<int32_t>(mat_idx),
            material.diffuse.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        );

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

        bool res = true;

        Mesh mesh;

        uint64_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
        res &= m_engine.create_buffer(
            vertex_buffer_size,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            D3D12_HEAP_TYPE_DEFAULT,
            mesh.vertex_buffer
        );

        res &= m_engine.upload_to_buffer(
            mesh.vertex_buffer.Get(),
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            vertices.data(),
            vertex_buffer_size
        );

        uint64_t index_buffer_size = indices.size() * sizeof(uint32_t);
        res &= m_engine.create_buffer(
            index_buffer_size,
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            D3D12_HEAP_TYPE_DEFAULT,
            mesh.index_buffer
        );

        res &= m_engine.upload_to_buffer(
            mesh.index_buffer.Get(),
            D3D12_RESOURCE_STATE_INDEX_BUFFER,
            indices.data(),
            index_buffer_size
        );

        if (!res)
        {
            spdlog::error(
                "App::load_scene: failed to create vertex and/or index buffers for mesh #{}",
                mesh_idx
            );
        }

        mesh.vertex_buffer_view.BufferLocation = mesh.vertex_buffer->GetGPUVirtualAddress();
        mesh.vertex_buffer_view.StrideInBytes = sizeof(Vertex);
        mesh.vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertex_buffer_size);

        mesh.index_buffer_view.BufferLocation = mesh.index_buffer->GetGPUVirtualAddress();
        mesh.index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
        mesh.index_buffer_view.SizeInBytes = static_cast<UINT>(index_buffer_size);

        mesh.index_count = static_cast<uint32_t>(indices.size());

        mesh.material_idx = ai_mesh->mMaterialIndex;

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
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    build_ui();

    bool res = m_engine.render_frame([&](ID3D12GraphicsCommandList *cmd_list,
                                         ID3D12Resource *target,
                                         D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle) {
        m_shadow_map_pass.run(cmd_list, m_scene);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_shadow_map_pass.shadow_map(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        m_forward_pass.run(cmd_list, m_scene);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_shadow_map_pass.shadow_map(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_pass.color_target(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        );
        cmd_list->ResourceBarrier(1, &barrier);

        m_post_process_pass.run(cmd_list, m_gamma);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_pass.color_target(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE
        );
        cmd_list->ResourceBarrier(1, &barrier);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            target,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        cmd_list->ResourceBarrier(1, &barrier);

        cmd_list->CopyResource(target, m_forward_pass.color_target());

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            target,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_pass.color_target(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);

        ImGui::Render();
        std::array descriptor_heaps{m_imgui_cbv_srv_heap.Get()};
        cmd_list->SetDescriptorHeaps(1, descriptor_heaps.data());
        cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd_list);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            target,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT
        );
        cmd_list->ResourceBarrier(1, &barrier);
    });
    if (!res)
    {
        spdlog::error("App::render_frame: failed to render frame");
        return false;
    }

    return true;
}

void App::build_ui()
{
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
        ImGui::SeparatorText("Stats");
        ImGui::Text("Frame Time: %.2f ms", m_delta_time * 1000.0f);

        ImGui::SeparatorText("Camera");
        ImGui::SliderFloat("Speed", &m_camera_speed, 0.1f, 5000.0f);
        ImGui::SliderFloat("Sensitivity", &m_mouse_sensitivity, 0.01f, 2.0f);
        ImGui::DragFloat3("Position", &m_scene.camera.eye.x, 0.1f);
        ImGui::DragFloat2("Rotation", &m_scene.camera.rotation.x, 0.1f, -360.0f, 360.0f);
        ImGui::DragFloat2("Z Near/Far", m_scene.camera.z_near_far.data(), 0.01f, 0.001f, 10000.0f);

        ImGui::SeparatorText("Light");
        ImGui::SliderFloat("Ambient", &m_scene.ambient, 0.0f, 1.0f);
        ImGui::DragFloat3("Sun Position", &m_scene.sun.position.x);
        ImGui::DragFloat2("Sun Rotation", &m_scene.sun.rotation.x, 0.1f, -360.0f, 360.0f);
        ImGui::ColorPicker3("Sun Color", &m_scene.sun.color.x);

        ImGui::SeparatorText("Post Processing");
        ImGui::DragFloat("Gamma", &m_gamma, 0.01f, 0.1f, 5.0f);
    }
    ImGui::End();
}

bool App::handle_resize()
{
    int window_width, window_height;
    assert(SDL_GetWindowSize(m_window, &window_width, &window_height));

    uint32_t width = static_cast<uint32_t>(window_width);
    uint32_t height = static_cast<uint32_t>(window_height);
    if (width == m_window_size.width && height == m_window_size.height)
    {
        return true;
    }

    m_window_size.width = std::max(1u, width);
    m_window_size.height = std::max(1u, height);

    if (!m_engine.flush())
    {
        spdlog::error("App::handle_resize: failed to flush");
        return false;
    }

    if (!m_engine.resize(m_window_size.width, m_window_size.height))
    {
        spdlog::error("App::handle_resize: failed to resize engine resources");
        return false;
    }

    if (!m_forward_pass.resize(m_window_size.width, m_window_size.height))
    {
        spdlog::error("App::handle_resize: failed to resize forward pass resources");
        return false;
    }

    if (!m_post_process_pass.resize(m_window_size.width, m_window_size.height))
    {
        spdlog::error("App::handle_resize: failed to resize post process pass resources");
        return false;
    }

    m_scene.camera.aspect =
        static_cast<float>(m_window_size.width) / static_cast<float>(m_window_size.height);

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
