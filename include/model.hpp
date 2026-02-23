#pragma once

#include <array>
#include <string>
#include <vector>

#include "math.hpp"

class Model {
public:
    explicit Model(const std::string& path);

    std::array<int, 3> face_vertex_indices(size_t face_id) const;
    std::array<int, 3> face_normal_indices(size_t face_id) const;
    Vec3f vertex(int index) const;
    Vec3f normal(int index) const;
    size_t face_count() const { return faces_.size(); }

private:
    struct Face {
        std::array<int, 3> vertex_ids{};
        std::array<int, 3> normal_ids{};
    };

    std::vector<Vec3f> vertices_;
    std::vector<Vec3f> normals_;
    std::vector<Face> faces_;
};
