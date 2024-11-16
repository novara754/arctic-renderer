#include "scene.hpp"

DirectX::XMFLOAT3 Camera::forward() const
{
    using namespace DirectX;

    return XMFLOAT3(
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarCos(XMConvertToRadians(this->rotation.y)),
        XMScalarSin(XMConvertToRadians(this->rotation.x)),
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarSin(XMConvertToRadians(this->rotation.y))
    );
}

DirectX::XMMATRIX Camera::view_matrix() const
{
    using namespace DirectX;

    XMFLOAT3 forward = this->forward();
    XMFLOAT3 up = this->up();

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

DirectX::XMFLOAT3 DirectionalLight::direction() const
{
    using namespace DirectX;

    return XMFLOAT3(
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarCos(XMConvertToRadians(this->rotation.y)),
        XMScalarSin(XMConvertToRadians(this->rotation.x)),
        XMScalarCos(XMConvertToRadians(this->rotation.x)) *
            XMScalarSin(XMConvertToRadians(this->rotation.y))
    );
}
