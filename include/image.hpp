#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math.hpp"

class Image {
public:
    Image(int width, int height);

    void clear(const Vec3f& color);
    void set_pixel(int x, int y, const Vec3f& color);
    bool write_png(const std::string& path) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_;
};
