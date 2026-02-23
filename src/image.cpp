#include "image.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Image::Image(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height * 3, 0) {}

void Image::clear(const Vec3f& color) {
    const uint8_t r = static_cast<uint8_t>(clamp(color.x, 0.f, 1.f) * 255.f);
    const uint8_t g = static_cast<uint8_t>(clamp(color.y, 0.f, 1.f) * 255.f);
    const uint8_t b = static_cast<uint8_t>(clamp(color.z, 0.f, 1.f) * 255.f);
    for (size_t i = 0; i < pixels_.size(); i += 3) {
        pixels_[i] = r;
        pixels_[i + 1] = g;
        pixels_[i + 2] = b;
    }
}

void Image::set_pixel(int x, int y, const Vec3f& color) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return;
    }
    size_t index = (static_cast<size_t>(y) * width_ + x) * 3;
    pixels_[index] = static_cast<uint8_t>(clamp(color.x, 0.f, 1.f) * 255.f);
    pixels_[index + 1] = static_cast<uint8_t>(clamp(color.y, 0.f, 1.f) * 255.f);
    pixels_[index + 2] = static_cast<uint8_t>(clamp(color.z, 0.f, 1.f) * 255.f);
}

bool Image::write_png(const std::string& path) const {
    int stride = width_ * 3;
    return stbi_write_png(path.c_str(), width_, height_, 3, pixels_.data(), stride) != 0;
}
