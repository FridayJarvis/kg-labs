#pragma once

#include <array>

#include "math.hpp"

struct VertexInput {
    Vec3f position;
    Vec3f normal;
};

struct VertexOutput {
    Vec4f clip_position;
    Vec3f world_position;
    Vec3f normal;
    float reciprocal_w = 1.f;
};

class IShader {
public:
    virtual ~IShader() = default;
    virtual VertexOutput vertex(const VertexInput& in) = 0;
    virtual Vec3f fragment(const Vec3f& barycentric,
                           const std::array<VertexOutput, 3>& data) const = 0;
};

class PhongShader : public IShader {
public:
    void set_matrices(const Mat4f& model,
                      const Mat4f& view,
                      const Mat4f& projection);
    void set_light_direction(const Vec3f& dir);
    void set_light_color(const Vec3f& color);
    void set_fill_light(const Vec3f& dir, const Vec3f& color);
    void set_view_position(const Vec3f& pos);
    void set_material(const Vec3f& ambient,
                      const Vec3f& diffuse,
                      const Vec3f& specular,
                      float shininess);
    void set_exposure(float exposure);

    VertexOutput vertex(const VertexInput& in) override;
    Vec3f fragment(const Vec3f& barycentric,
                   const std::array<VertexOutput, 3>& data) const override;

private:
    Mat4f model_ = Mat4f::identity();
    Mat4f view_ = Mat4f::identity();
    Mat4f projection_ = Mat4f::identity();
    Mat4f mvp_ = Mat4f::identity();

    Vec3f light_dir_{0.f, 0.f, -1.f};
    Vec3f light_color_{1.f, 1.f, 1.f};
    Vec3f fill_light_dir_{0.f, 0.f, -1.f};
    Vec3f fill_light_color_{0.f, 0.f, 0.f};
    Vec3f view_pos_{0.f, 0.f, 3.f};

    Vec3f ambient_{0.1f, 0.1f, 0.1f};
    Vec3f diffuse_{0.7f, 0.7f, 0.7f};
    Vec3f specular_{0.3f, 0.3f, 0.3f};
    float shininess_ = 32.f;
    float exposure_ = 1.f;
};
