#pragma once

#include <array>
#include <vector>

#include <DirectXMath.h>
#include <d3d12.h>

#include "comptr.hpp"

struct Camera
{
    DirectX::XMFLOAT3 eye;
    DirectX::XMFLOAT2 rotation;
    float aspect;
    float fov_y;
    std::array<float, 2> z_near_far;

    [[nodiscard]] DirectX::XMFLOAT3 forward() const;

    [[nodiscard]] DirectX::XMFLOAT3 up() const
    {
        return DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
    }

    [[nodiscard]] DirectX::XMMATRIX view_matrix() const;

    [[nodiscard]] DirectX::XMMATRIX proj_matrix() const;
};

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

struct Material
{
    ComPtr<ID3D12Resource> diffuse;
};

struct DirectionalLight
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 rotation;
    DirectX::XMFLOAT3 color;

    [[nodiscard]] DirectX::XMFLOAT3 direction() const;

    [[nodiscard]] DirectX::XMMATRIX view_matrix() const;

    [[nodiscard]] DirectX::XMMATRIX proj_matrix() const;
};

struct Scene
{
    Camera camera;
    float ambient;
    DirectionalLight sun;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<size_t> objects;
};
