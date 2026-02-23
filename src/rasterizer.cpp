#include "rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

Rasterizer::Rasterizer(int width, int height)
    : color_buffer_(width, height),
      depth_buffer_(static_cast<size_t>(width) * height, std::numeric_limits<float>::infinity()) {
    color_buffer_.clear({0.f, 0.f, 0.f});
}

Vec3f Rasterizer::barycentric(const std::array<float, 2>& a,
                             const std::array<float, 2>& b,
                             const std::array<float, 2>& c,
                             float px,
                             float py) {
    Vec3f u{b[0] - a[0], c[0] - a[0], a[0] - px};
    Vec3f v{b[1] - a[1], c[1] - a[1], a[1] - py};
    Vec3f cross_prod = cross(u, v);
    if (std::abs(cross_prod.z) < 1e-8f) {
        return {-1.f, 1.f, 1.f};
    }
    float inv = 1.f / cross_prod.z;
    float u_coord = cross_prod.x * inv;
    float v_coord = cross_prod.y * inv;
    return {1.f - u_coord - v_coord, u_coord, v_coord};
}

void Rasterizer::render(const Model& model, IShader& shader) {
    color_buffer_.clear({0.f, 0.f, 0.f});
    std::fill(depth_buffer_.begin(), depth_buffer_.end(), std::numeric_limits<float>::infinity());

    int width = color_buffer_.width();
    int height = color_buffer_.height();

    std::array<RasterVertex, 3> verts;

    for (size_t face = 0; face < model.face_count(); ++face) {
        auto vertex_ids = model.face_vertex_indices(face);
        auto normal_ids = model.face_normal_indices(face);

        for (int i = 0; i < 3; ++i) {
            VertexInput input{model.vertex(vertex_ids[i]), model.normal(normal_ids[i])};
            VertexOutput output = shader.vertex(input);
            float inv_w = output.reciprocal_w;
            Vec3f ndc{output.clip_position.x * inv_w,
                      output.clip_position.y * inv_w,
                      output.clip_position.z * inv_w};

            verts[i].screen_pos = {
                (ndc.x + 1.f) * 0.5f * static_cast<float>(width - 1),
                (1.f - (ndc.y + 1.f) * 0.5f) * static_cast<float>(height - 1)
            };
            verts[i].depth = (ndc.z + 1.f) * 0.5f;
            verts[i].payload = output;
        }

        float min_x = std::min({verts[0].screen_pos[0], verts[1].screen_pos[0], verts[2].screen_pos[0]});
        float max_x = std::max({verts[0].screen_pos[0], verts[1].screen_pos[0], verts[2].screen_pos[0]});
        float min_y = std::min({verts[0].screen_pos[1], verts[1].screen_pos[1], verts[2].screen_pos[1]});
        float max_y = std::max({verts[0].screen_pos[1], verts[1].screen_pos[1], verts[2].screen_pos[1]});

        int x0 = static_cast<int>(std::floor(std::max(0.f, min_x)));
        int x1 = static_cast<int>(std::ceil(std::min(static_cast<float>(width - 1), max_x)));
        int y0 = static_cast<int>(std::floor(std::max(0.f, min_y)));
        int y1 = static_cast<int>(std::ceil(std::min(static_cast<float>(height - 1), max_y)));

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float px = static_cast<float>(x) + 0.5f;
                float py = static_cast<float>(y) + 0.5f;
                Vec3f bary = barycentric(verts[0].screen_pos, verts[1].screen_pos, verts[2].screen_pos, px, py);
                if (bary.x < 0.f || bary.y < 0.f || bary.z < 0.f) {
                    continue;
                }
                float depth = bary.x * verts[0].depth +
                              bary.y * verts[1].depth +
                              bary.z * verts[2].depth;
                size_t index = static_cast<size_t>(y) * width + x;
                if (depth < depth_buffer_[index]) {
                    Vec3f color = shader.fragment(bary, {verts[0].payload, verts[1].payload, verts[2].payload});
                    color_buffer_.set_pixel(x, y, color);
                    depth_buffer_[index] = depth;
                }
            }
        }
    }
}

bool Rasterizer::write_png(const std::string& path) const {
    return color_buffer_.write_png(path);
}
