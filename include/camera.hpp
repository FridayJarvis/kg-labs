#pragma once

#include "math.hpp"

class Camera {
public:
    Camera(Vec3f position,
           Vec3f target,
           Vec3f up_dir,
           float fov_degrees,
           float aspect_ratio,
           float near_plane,
           float far_plane)
        : position_(position),
          target_(target),
          up_(normalize(up_dir)),
          fov_(fov_degrees),
          aspect_(aspect_ratio),
          near_(near_plane),
          far_(far_plane) {}

    Mat4f view_matrix() const {
        return Mat4f::look_at(position_, target_, up_);
    }

    Mat4f projection_matrix() const {
        return Mat4f::perspective(radians(fov_), aspect_, near_, far_);
    }

    const Vec3f& position() const { return position_; }

private:
    Vec3f position_{};
    Vec3f target_{};
    Vec3f up_{0.f, 1.f, 0.f};
    float fov_ = 45.f;
    float aspect_ = 1.f;
    float near_ = 0.1f;
    float far_ = 100.f;
};
