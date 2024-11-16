#include "scene.hpp"

DirectX::XMMATRIX Camera::view_matrix() const
{
    using namespace DirectX;

    XMFLOAT3 forward(
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarCos(XMConvertToRadians(this->rotation.y)),
        XMScalarSin(XMConvertToRadians(this->rotation.x)),
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarSin(XMConvertToRadians(this->rotation.y))
    );

    XMFLOAT3 up(0.0f, 1.0f, 0.0f);

    return XMMatrixLookToRH(XMLoadFloat3(&this->eye), XMLoadFloat3(&forward), XMLoadFloat3(&up));
}

DirectX::XMMATRIX Camera::proj_matrix() const
{
    using namespace DirectX;

    return XMMatrixPerspectiveFovRH(
        XMConvertToRadians(this->fov_y),
        this->aspect,
        this->z_near_far[0],
        this->z_near_far[1]
    );
}
