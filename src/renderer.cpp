#include "renderer.hpp"

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

bool Renderer::init()
{
    if (!m_rhi.init(m_window, m_window_size.width, m_window_size.height))
    {
        spdlog::error("Renderer::init: failed to initialize rhi");
        return false;
    }

    if (!m_shadow_map_pass.init())
    {
        spdlog::error("Renderer::init: failed to initialize forward pass");
        return false;
    }

    if (!m_forward_pass
             .init(m_window_size.width, m_window_size.height, m_shadow_map_pass.shadow_map()))
    {
        spdlog::error("Renderer::init: failed to initialize forward pass");
        return false;
    }

    if (!m_post_process_pass.init(
            m_window_size.width,
            m_window_size.height,
            m_forward_pass.color_target(),
            m_rhi.swapchain_format()
        ))
    {
        spdlog::error("Renderer::init: failed to initialize post process pass");
        return false;
    }

    {
        if (!m_rhi.create_descriptor_heap(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                1,
                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                m_imgui_cbv_srv_heap
            ))
        {
            spdlog::error("Renderer::init: failed to create cbv srv heap for imgui");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;

        ImGui_ImplSDL3_InitForOther(m_window);
        ImGui_ImplDX12_Init(
            m_rhi.device(),
            RHI::NUM_FRAMES,
            m_rhi.swapchain_format(),
            m_imgui_cbv_srv_heap.Get(),
            m_imgui_cbv_srv_heap->GetCPUDescriptorHandleForHeapStart(),
            m_imgui_cbv_srv_heap->GetGPUDescriptorHandleForHeapStart()
        );
        spdlog::trace("Renderer::init: initialized imgui");
    }

    return true;
}

bool Renderer::resize(uint32_t &out_width, uint32_t &out_height)
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

    if (!m_rhi.flush())
    {
        spdlog::error("App::handle_resize: failed to flush");
        return false;
    }

    if (!m_rhi.resize(m_window_size.width, m_window_size.height))
    {
        spdlog::error("App::handle_resize: failed to resize rhi resources");
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

    out_width = static_cast<uint32_t>(window_width);
    out_height = static_cast<uint32_t>(window_height);

    return true;
}

bool Renderer::render_frame(
    const Scene &scene, const Settings &settings, std::function<void()> &&build_ui
)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    build_ui();

    bool res = m_rhi.render_frame([&](ID3D12GraphicsCommandList *cmd_list,
                                      ID3D12Resource *target,
                                      D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle) {
        m_shadow_map_pass.run(cmd_list, scene);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_shadow_map_pass.shadow_map(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        m_forward_pass.run(cmd_list, scene);
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

        m_post_process_pass.run(
            cmd_list,
            static_cast<uint32_t>(settings.tm_method),
            settings.gamma,
            settings.exposure
        );

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_pass.color_target(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_post_process_pass.output_texture(),
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

        cmd_list->CopyResource(target, m_post_process_pass.output_texture());

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            target,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_post_process_pass.output_texture(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
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

bool Renderer::create_mesh(
    Mesh &out_mesh, std::span<Vertex> vertices, std::span<uint32_t> indices, size_t material_idx
)
{
    uint64_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
    bool res = m_rhi.create_buffer(
        vertex_buffer_size,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_HEAP_TYPE_DEFAULT,
        out_mesh.vertex_buffer
    );

    res &= m_rhi.upload_to_buffer(
        out_mesh.vertex_buffer.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        vertices.data(),
        vertex_buffer_size
    );

    uint64_t index_buffer_size = indices.size() * sizeof(uint32_t);
    res &= m_rhi.create_buffer(
        index_buffer_size,
        D3D12_RESOURCE_STATE_INDEX_BUFFER,
        D3D12_HEAP_TYPE_DEFAULT,
        out_mesh.index_buffer
    );

    res &= m_rhi.upload_to_buffer(
        out_mesh.index_buffer.Get(),
        D3D12_RESOURCE_STATE_INDEX_BUFFER,
        indices.data(),
        index_buffer_size
    );

    if (!res)
    {
        spdlog::error("Renderer::create_mesh: failed to create vertex and/or index buffers");
    }

    out_mesh.vertex_buffer_view.BufferLocation = out_mesh.vertex_buffer->GetGPUVirtualAddress();
    out_mesh.vertex_buffer_view.StrideInBytes = sizeof(Vertex);
    out_mesh.vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertex_buffer_size);

    out_mesh.index_buffer_view.BufferLocation = out_mesh.index_buffer->GetGPUVirtualAddress();
    out_mesh.index_buffer_view.Format = DXGI_FORMAT_R32_UINT;
    out_mesh.index_buffer_view.SizeInBytes = static_cast<UINT>(index_buffer_size);

    out_mesh.index_count = static_cast<uint32_t>(indices.size());

    out_mesh.material_idx = material_idx;

    return true;
}

bool Renderer::create_material(
    Material &out_material, size_t material_idx, void *diffuse_data, uint32_t diffuse_width,
    uint32_t diffuse_height
)
{
    bool res = m_rhi.create_texture(
        diffuse_width,
        diffuse_height,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        out_material.diffuse
    );
    res &= m_rhi.upload_to_texture(
        out_material.diffuse.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        diffuse_data,
        diffuse_width,
        diffuse_height,
        4
    );
    if (!res)
    {
        spdlog::error("Renderer::create_material: failed to create diffuse texture");
        return false;
    }

    m_forward_pass.create_srv_tex2d(
        static_cast<int32_t>(material_idx),
        out_material.diffuse.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    );

    return true;
}
