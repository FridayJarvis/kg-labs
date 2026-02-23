#include "model.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
std::vector<std::string> tokenize_face(const std::string& token) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : token) {
        if (ch == '/') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}
}

Model::Model(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open OBJ file: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream stream(line);
        std::string prefix;
        stream >> prefix;
        if (prefix == "v") {
            float x, y, z;
            stream >> x >> y >> z;
            vertices_.push_back({x, y, z});
        } else if (prefix == "vn") {
            float x, y, z;
            stream >> x >> y >> z;
            normals_.push_back(normalize({x, y, z}));
        } else if (prefix == "f") {
            Face face{};
            for (int i = 0; i < 3; ++i) {
                std::string token;
                stream >> token;
                if (token.empty()) {
                    continue;
                }
                auto parts = tokenize_face(token);
                int v_id = parts.size() > 0 && !parts[0].empty() ? std::stoi(parts[0]) : 0;
                int n_id = parts.size() > 2 && !parts[2].empty() ? std::stoi(parts[2]) : 0;
                face.vertex_ids[i] = v_id - 1;
                face.normal_ids[i] = n_id > 0 ? n_id - 1 : -1;
            }
            faces_.push_back(face);
        }
    }

    if (normals_.empty()) {
        normals_.resize(vertices_.size(), Vec3f{});
        std::vector<int> counts(vertices_.size(), 0);
        for (auto& face : faces_) {
            Vec3f v0 = vertices_[face.vertex_ids[0]];
            Vec3f v1 = vertices_[face.vertex_ids[1]];
            Vec3f v2 = vertices_[face.vertex_ids[2]];
            Vec3f n = normalize(cross(v1 - v0, v2 - v0));
            for (int i = 0; i < 3; ++i) {
                int idx = face.vertex_ids[i];
                normals_[idx] += n;
                counts[idx] += 1;
                face.normal_ids[i] = idx;
            }
        }
        for (size_t i = 0; i < normals_.size(); ++i) {
            if (counts[i] > 0) {
                normals_[i] = normalize(normals_[i]);
            }
        }
    } else {
        for (auto& face : faces_) {
            for (int i = 0; i < 3; ++i) {
                if (face.normal_ids[i] < 0) {
                    face.normal_ids[i] = face.vertex_ids[i];
                }
            }
        }
    }
}

std::array<int, 3> Model::face_vertex_indices(size_t face_id) const {
    return faces_.at(face_id).vertex_ids;
}

std::array<int, 3> Model::face_normal_indices(size_t face_id) const {
    return faces_.at(face_id).normal_ids;
}

Vec3f Model::vertex(int index) const {
    return vertices_.at(static_cast<size_t>(index));
}

Vec3f Model::normal(int index) const {
    return normals_.at(static_cast<size_t>(index));
}
