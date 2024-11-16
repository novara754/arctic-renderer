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

struct Scene
{
    Camera camera;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<size_t> objects;
};
