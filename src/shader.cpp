#include "shader.hpp"

void PhongShader::set_matrices(const Mat4f& model,
                               const Mat4f& view,
                               const Mat4f& projection) {
    model_ = model;
    view_ = view;
    projection_ = projection;
    mvp_ = projection_ * view_ * model_;
}

void PhongShader::set_light_direction(const Vec3f& dir) {
    light_dir_ = normalize(dir);
}

void PhongShader::set_light_color(const Vec3f& color) {
    light_color_ = color;
}

void PhongShader::set_fill_light(const Vec3f& dir, const Vec3f& color) {
    fill_light_dir_ = normalize(dir);
    fill_light_color_ = color;
}

void PhongShader::set_view_position(const Vec3f& pos) {
    view_pos_ = pos;
}

void PhongShader::set_material(const Vec3f& ambient,
                               const Vec3f& diffuse,
                               const Vec3f& specular,
                               float shininess) {
    ambient_ = ambient;
    diffuse_ = diffuse;
    specular_ = specular;
    shininess_ = shininess;
}

void PhongShader::set_exposure(float exposure) {
    exposure_ = exposure;
}

VertexOutput PhongShader::vertex(const VertexInput& in) {
    VertexOutput out;
    out.clip_position = mvp_ * to_vec4(in.position, 1.f);
    Vec4f world = model_ * to_vec4(in.position, 1.f);
    out.world_position = {world.x, world.y, world.z};
    out.normal = transform_direction(model_, in.normal);
    out.reciprocal_w = 1.f / out.clip_position.w;
    return out;
}

Vec3f PhongShader::fragment(const Vec3f& barycentric,
                            const std::array<VertexOutput, 3>& data) const {
    float w0 = barycentric.x * data[0].reciprocal_w;
    float w1 = barycentric.y * data[1].reciprocal_w;
    float w2 = barycentric.z * data[2].reciprocal_w;
    float sum = w0 + w1 + w2;
    if (sum == 0.f) {
        return ambient_;
    }
    w0 /= sum;
    w1 /= sum;
    w2 /= sum;

    Vec3f position = data[0].world_position * w0 +
                     data[1].world_position * w1 +
                     data[2].world_position * w2;
    Vec3f normal = normalize(data[0].normal * w0 +
                             data[1].normal * w1 +
                             data[2].normal * w2);

    Vec3f light_dir = normalize(-light_dir_);
    float diff = std::max(dot(normal, light_dir), 0.f);
    Vec3f diffuse = diffuse_ * diff;

    Vec3f view_dir = normalize(view_pos_ - position);
    Vec3f reflect_dir = normalize(2.f * dot(normal, light_dir) * normal - light_dir);
    float spec = std::pow(std::max(dot(view_dir, reflect_dir), 0.f), shininess_);
    Vec3f specular = specular_ * spec;

    Vec3f color = (ambient_ + diffuse + specular) * exposure_;
    Vec3f keyed = hadamard(color, light_color_);

    Vec3f fill_color = {0.f, 0.f, 0.f};
    if (length(fill_light_color_) > 0.f) {
        float fill_diff = std::max(dot(normal, normalize(-fill_light_dir_)), 0.f);
        fill_color = hadamard(diffuse_ * fill_diff, fill_light_color_);
    }

    return keyed + fill_color;
}
