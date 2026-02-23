#pragma once

#include <array>
#include <vector>

#include "image.hpp"
#include "model.hpp"
#include "shader.hpp"

class Rasterizer {
public:
    Rasterizer(int width, int height);

    void render(const Model& model, IShader& shader);
    bool write_png(const std::string& path) const;
    const Image& image() const { return color_buffer_; }

private:
    struct RasterVertex {
        std::array<float, 2> screen_pos{};
        float depth = 0.f;
        VertexOutput payload;
    };

    Image color_buffer_;
    std::vector<float> depth_buffer_;

    static Vec3f barycentric(const std::array<float, 2>& a,
                             const std::array<float, 2>& b,
                             const std::array<float, 2>& c,
                             float px,
                             float py);
};
