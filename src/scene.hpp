#pragma once

#include <array>
#include <vector>

#include <d3d12.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "comptr.hpp"

struct Camera
{
    glm::vec3 eye;
    glm::vec2 rotation;
    float aspect;
    float fov_y;
    std::array<float, 2> z_near_far;

    [[nodiscard]] glm::vec3 forward() const;

    [[nodiscard]] glm::vec3 up() const
    {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    [[nodiscard]] glm::mat4 proj_view_matrix() const;
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec2 tex_coords;
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
    ComPtr<ID3D12Resource> normal;
};

struct Object
{
    glm::mat4 trs;
    size_t mesh_idx;
};

struct DirectionalLight
{
    glm::vec3 position;
    glm::vec2 rotation;
    glm::vec3 color;

    [[nodiscard]] glm::vec3 direction() const;

    [[nodiscard]] glm::mat4 proj_view_matrix() const;
};

struct Scene
{
    Camera camera;
    float ambient;
    DirectionalLight sun;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Object> objects;
};

struct Settings
{
    int tm_method{0};
    float gamma{2.2f};
    float exposure{1.0f};
};
