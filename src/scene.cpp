#include "scene.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

glm::vec3 dir_from_rot(const glm::vec2 &euler_rot_deg)
{
    float x_rad = glm::radians(euler_rot_deg.x);
    float y_rad = glm::radians(euler_rot_deg.y);

    return glm::vec3(
        glm::cos(x_rad) * glm::cos(y_rad),
        glm::sin(x_rad),
        glm::cos(x_rad) * glm::sin(y_rad)
    );
}

glm::vec3 Camera::forward() const
{
    return dir_from_rot(this->rotation);
}

glm::mat4 Camera::proj_view_matrix() const
{
    glm::vec3 forward = dir_from_rot(this->rotation);
    glm::vec3 up = this->up();

    glm::mat4 view = glm::lookAtRH(this->eye, this->eye + forward, up);
    glm::mat4 proj = glm::perspectiveRH(
        glm::radians(this->fov_y),
        this->aspect,
        this->z_near_far[0],
        this->z_near_far[1]
    );
    return proj * view;
}

glm::vec3 DirectionalLight::direction() const
{
    return dir_from_rot(this->rotation);
}

glm::mat4 DirectionalLight::proj_view_matrix() const
{
    glm::vec3 forward = this->direction();
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAtRH(this->position, this->position + forward, up);
    // glm::mat4 proj = glm::orthoRH(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 10.0f);
    glm::mat4 proj = glm::orthoRH(-16.0f, 16.0f, -16.0f, 16.0f, 0.1f, 50.0f);
    return proj * view;
}
