#pragma once

#include <cmath>
#include <array>
#include <algorithm>

struct Vec2f {
    float x = 0.f;
    float y = 0.f;
};

struct Vec3f {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    float& operator[](int i) {
        return *(&x + i);
    }

    float operator[](int i) const {
        return *(&x + i);
    }
};

struct Vec4f {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 1.f;
};

inline Vec3f operator+(const Vec3f& a, const Vec3f& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3f operator-(const Vec3f& a, const Vec3f& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3f operator-(const Vec3f& v) {
    return {-v.x, -v.y, -v.z};
}

inline Vec3f operator*(const Vec3f& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline Vec3f operator*(float s, const Vec3f& v) {
    return v * s;
}

inline Vec3f operator/(const Vec3f& v, float s) {
    return {v.x / s, v.y / s, v.z / s};
}

inline Vec3f& operator+=(Vec3f& a, const Vec3f& b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

inline Vec3f& operator-=(Vec3f& a, const Vec3f& b) {
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

inline Vec3f hadamard(const Vec3f& a, const Vec3f& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

inline float dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float length(const Vec3f& v) {
    return std::sqrt(dot(v, v));
}

inline Vec3f normalize(const Vec3f& v) {
    float len = length(v);
    if (len == 0.f) {
        return {0.f, 0.f, 0.f};
    }
    return v / len;
}

inline Vec4f to_vec4(const Vec3f& v, float w = 1.f) {
    return {v.x, v.y, v.z, w};
}

inline Vec3f to_vec3(const Vec4f& v) {
    return {v.x, v.y, v.z};
}

inline float clamp(float v, float min_v, float max_v) {
    return std::max(min_v, std::min(max_v, v));
}

inline float radians(float degrees) {
    return degrees * 0.01745329251994329577f;
}

struct Mat4f {
    std::array<std::array<float, 4>, 4> m{};

    static Mat4f identity() {
        Mat4f result = {};
        for (int i = 0; i < 4; ++i) {
            result.m[i][i] = 1.f;
        }
        return result;
    }

    const std::array<float, 4>& operator[](int row) const {
        return m[row];
    }

    std::array<float, 4>& operator[](int row) {
        return m[row];
    }

    static Mat4f translation(const Vec3f& t) {
        Mat4f result = identity();
        result[0][3] = t.x;
        result[1][3] = t.y;
        result[2][3] = t.z;
        return result;
    }

    static Mat4f scale(const Vec3f& s) {
        Mat4f result = {};
        result[0][0] = s.x;
        result[1][1] = s.y;
        result[2][2] = s.z;
        result[3][3] = 1.f;
        return result;
    }

    static Mat4f rotation_y(float angle) {
        Mat4f result = identity();
        float c = std::cos(angle);
        float s = std::sin(angle);
        result[0][0] = c;
        result[0][2] = s;
        result[2][0] = -s;
        result[2][2] = c;
        return result;
    }

    static Mat4f perspective(float fov_y, float aspect, float z_near, float z_far) {
        Mat4f result = {};
        float f = 1.f / std::tan(fov_y * 0.5f);
        result[0][0] = f / aspect;
        result[1][1] = f;
        result[2][2] = (z_far + z_near) / (z_near - z_far);
        result[2][3] = (2.f * z_far * z_near) / (z_near - z_far);
        result[3][2] = -1.f;
        return result;
    }

    static Mat4f look_at(const Vec3f& eye, const Vec3f& center, const Vec3f& up) {
        Vec3f f = normalize(center - eye);
        Vec3f s = normalize(cross(f, up));
        Vec3f u = cross(s, f);

        Mat4f result = identity();
        result[0][0] = s.x;
        result[0][1] = s.y;
        result[0][2] = s.z;
        result[1][0] = u.x;
        result[1][1] = u.y;
        result[1][2] = u.z;
        result[2][0] = -f.x;
        result[2][1] = -f.y;
        result[2][2] = -f.z;
        result[0][3] = -dot(s, eye);
        result[1][3] = -dot(u, eye);
        result[2][3] = dot(f, eye);
        return result;
    }
};

inline Mat4f operator*(const Mat4f& a, const Mat4f& b) {
    Mat4f result = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float value = 0.f;
            for (int k = 0; k < 4; ++k) {
                value += a.m[row][k] * b.m[k][col];
            }
            result.m[row][col] = value;
        }
    }
    return result;
}

inline Vec4f operator*(const Mat4f& m, const Vec4f& v) {
    Vec4f result;
    result.x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w;
    result.y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w;
    result.z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w;
    result.w = m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w;
    return result;
}

inline Vec3f transform_direction(const Mat4f& m, const Vec3f& v) {
    Vec4f tmp = m * Vec4f{v.x, v.y, v.z, 0.f};
    return normalize(Vec3f{tmp.x, tmp.y, tmp.z});
}

inline Vec3f transform_point(const Mat4f& m, const Vec3f& v) {
    Vec4f tmp = m * Vec4f{v.x, v.y, v.z, 1.f};
    if (tmp.w == 0.f) {
        return {tmp.x, tmp.y, tmp.z};
    }
    return {tmp.x / tmp.w, tmp.y / tmp.w, tmp.z / tmp.w};
}
