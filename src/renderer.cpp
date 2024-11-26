#include "renderer.hpp"

#include <directx/d3dx12.h>

#include <spdlog/spdlog.h>

#include "imgui_impl_dx12.h"
#include "imgui_impl_sdl3.h"

#include "implot.h"

bool Renderer::init()
{
    if (!m_rhi.init(m_window, m_window_size.width, m_window_size.height))
    {
        spdlog::error("Renderer::init: failed to initialize rhi");
        return false;
    }

    if (!m_rhi.create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            256,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_rtv_heap
        ))
    {
        spdlog::error("Renderer::init: failed to create rtv heap");
        return false;
    }
    m_rtv_descriptor_size =
        m_rhi.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (!m_rhi.create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
            256,
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            m_dsv_heap
        ))
    {
        spdlog::error("Renderer::init: failed to create dsv heap");
        return false;
    }
    m_dsv_descriptor_size =
        m_rhi.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    if (!m_rhi.create_descriptor_heap(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            256,
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
            m_cbv_srv_uav_heap
        ))
    {
        spdlog::error("Renderer::init: failed to create cbv srv uav heap");
        return false;
    }
    m_cbv_srv_uav_descriptor_size =
        m_rhi.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!m_rhi.create_texture(
            ShadowMapPass::SIZE,
            ShadowMapPass::SIZE,
            DXGI_FORMAT_R32_TYPELESS,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            m_sun_shadow_map,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        ))
    {
        spdlog::error("Renderer::init: failed to create sun shadow map");
        return false;
    }
    m_sun_shadow_map->SetName(L"sun shadow map texture");
    m_sun_shadow_map_dsv = create_dsv(m_sun_shadow_map.Get());

    if (!m_rhi.create_texture(
            m_window_size.width,
            m_window_size.height,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            m_forward_color_target,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        ))
    {
        spdlog::error("Renderer::init: failed to create forward pass color target texture");
        return false;
    }
    m_forward_color_target->SetName(L"forward color target texture");
    m_forward_color_target_rtv =
        create_rtv(m_forward_color_target.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (!m_rhi.create_texture(
            m_window_size.width,
            m_window_size.height,
            DXGI_FORMAT_D32_FLOAT,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            m_forward_depth_target,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        ))
    {
        spdlog::error("Renderer::init: failed to create forward pass depth target texture");
        return false;
    }
    m_forward_depth_target->SetName(L"forward depth target texture");
    m_forward_depth_target_dsv = create_dsv(m_forward_depth_target.Get());

    if (!m_rhi.create_texture(
            m_window_size.width,
            m_window_size.height,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            m_post_process_output,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        ))
    {
        spdlog::error("Renderer::init: failed to create post processing pass output texture");
        return false;
    }
    m_post_process_output->SetName(L"post process output texture");

    if (!m_shadow_map_pass.init())
    {
        spdlog::error("Renderer::init: failed to initialize forward pass");
        return false;
    }

    if (!m_forward_pass.init())
    {
        spdlog::error("Renderer::init: failed to initialize forward pass");
        return false;
    }

    if (!m_post_process_pass.init())
    {
        spdlog::error("Renderer::init: failed to initialize post process pass");
        return false;
    }

    m_post_process_descriptors_base_handle =
        create_uav(m_forward_color_target.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    create_uav(m_post_process_output.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

    m_forward_descriptors_base_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_cbv_srv_uav_heap->GetGPUDescriptorHandleForHeapStart(),
        m_cbv_srv_uav_count,
        m_cbv_srv_uav_descriptor_size
    );

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
        ImPlot::CreateContext();
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

void Renderer::cleanup()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

bool Renderer::resize(uint32_t &out_width, uint32_t &out_height)
{
    int window_width{}, window_height{};
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
        cmd_list->SetDescriptorHeaps(1, m_cbv_srv_uav_heap.GetAddressOf());

        m_shadow_map_pass.run(cmd_list, m_sun_shadow_map_dsv, scene);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_sun_shadow_map.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmd_list->ResourceBarrier(1, &barrier);
        m_forward_pass.run(
            cmd_list,
            m_forward_color_target_rtv,
            m_forward_depth_target_dsv,
            m_forward_descriptors_base_handle,
            m_cbv_srv_uav_descriptor_size,
            m_window_size.width,
            m_window_size.height,
            scene
        );
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_sun_shadow_map.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );
        cmd_list->ResourceBarrier(1, &barrier);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_color_target.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        );
        cmd_list->ResourceBarrier(1, &barrier);

        m_post_process_pass.run(
            cmd_list,
            m_post_process_descriptors_base_handle,
            m_window_size.width,
            m_window_size.height,
            static_cast<uint32_t>(settings.tm_method),
            settings.gamma,
            settings.exposure
        );

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_forward_color_target.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_post_process_output.Get(),
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

        cmd_list->CopyResource(target, m_post_process_output.Get());

        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            target,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        cmd_list->ResourceBarrier(1, &barrier);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_post_process_output.Get(),
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
    Material &out_material, void *diffuse_data, uint32_t diffuse_width, uint32_t diffuse_height,
    void *normal_data, uint32_t normal_width, uint32_t normal_height,
    void *metalness_roughness_data, uint32_t metalness_roughness_width,
    uint32_t metalness_roughness_height
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

    res = m_rhi.create_texture(
        normal_width,
        normal_height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        out_material.normal
    );
    res &= m_rhi.upload_to_texture(
        out_material.normal.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        normal_data,
        normal_width,
        normal_height,
        4
    );
    if (!res)
    {
        spdlog::error("Renderer::create_material: failed to create normal texture");
        return false;
    }

    res = m_rhi.create_texture(
        metalness_roughness_width,
        metalness_roughness_height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        out_material.metalness_roughness
    );
    res &= m_rhi.upload_to_texture(
        out_material.metalness_roughness.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        metalness_roughness_data,
        metalness_roughness_width,
        metalness_roughness_height,
        4
    );
    if (!res)
    {
        spdlog::error("Renderer::create_material: failed to create metalness/roughness texture");
        return false;
    }

    create_srv(m_sun_shadow_map.Get(), DXGI_FORMAT_R32_FLOAT);
    create_srv(out_material.diffuse.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    create_srv(out_material.normal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    create_srv(out_material.metalness_roughness.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE
Renderer::create_rtv(ID3D12Resource *resource, DXGI_FORMAT format)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
        m_rtv_count,
        m_rtv_descriptor_size
    );
    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;
    m_rhi.device()->CreateRenderTargetView(resource, &desc, handle);

    ++m_rtv_count;

    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE
Renderer::create_dsv(ID3D12Resource *resource)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_dsv_heap->GetCPUDescriptorHandleForHeapStart(),
        m_dsv_count,
        m_dsv_descriptor_size
    );
    D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    m_rhi.device()->CreateDepthStencilView(resource, &desc, handle);

    ++m_dsv_count;

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Renderer::create_srv(ID3D12Resource *resource, DXGI_FORMAT format)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart(),
        m_cbv_srv_uav_count,
        m_cbv_srv_uav_descriptor_size
    );
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = 1;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.PlaneSlice = 0;
    desc.Texture2D.ResourceMinLODClamp = 0.0f;
    m_rhi.device()->CreateShaderResourceView(resource, &desc, handle);

    ++m_cbv_srv_uav_count;

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_cbv_srv_uav_heap->GetGPUDescriptorHandleForHeapStart(),
        m_cbv_srv_uav_count - 1,
        m_cbv_srv_uav_descriptor_size
    );
}

D3D12_GPU_DESCRIPTOR_HANDLE Renderer::create_uav(ID3D12Resource *resource, DXGI_FORMAT format)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        m_cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart(),
        m_cbv_srv_uav_count,
        m_cbv_srv_uav_descriptor_size
    );
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;
    m_rhi.device()->CreateUnorderedAccessView(resource, nullptr, &desc, handle);

    ++m_cbv_srv_uav_count;

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_cbv_srv_uav_heap->GetGPUDescriptorHandleForHeapStart(),
        m_cbv_srv_uav_count - 1,
        m_cbv_srv_uav_descriptor_size
    );
}
