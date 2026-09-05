// 最小 3D 数学。纯 C++，不依赖平台和引擎。
//
// 矩阵按**行主序**存放，乘法约定是「行向量乘矩阵」：v' = v * M。
// 着色器里对应要写 row_major，否则会静默转置，模型就没了。

#pragma once

#include <cmath>

namespace pet {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 v) {
    const float n = std::sqrt(dot(v, v));
    return n > 1e-8f ? v * (1.0f / n) : Vec3{0, 0, 0};
}

// 行主序 4x4。m[行][列]。
struct Mat4 {
    float m[4][4]{};

    static Mat4 identity() {
        Mat4 r;
        for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

inline Mat4 translate(float x, float y, float z) {
    Mat4 r = Mat4::identity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}

inline Mat4 scale(float x, float y, float z) {
    Mat4 r = Mat4::identity();
    r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
    return r;
}

// 三个旋转的符号约定（行向量乘矩阵，已用数值核过）：
//   rotate_y 正角：+z 转向 +x
//   rotate_x 正角：+z 转向 +y（抬头）
//   rotate_z 正角：+x 转向 +y（右侧抬起）
inline Mat4 rotate_y(float rad) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0][0] = c;  r.m[0][2] = -s;
    r.m[2][0] = s;  r.m[2][2] = c;
    return r;
}

inline Mat4 rotate_x(float rad) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[1][1] = c;  r.m[1][2] = -s;
    r.m[2][1] = s;  r.m[2][2] = c;
    return r;
}

inline Mat4 rotate_z(float rad) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0][0] = c;  r.m[0][1] = s;
    r.m[1][0] = -s; r.m[1][1] = c;
    return r;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 指数平滑：v 以时间常数 tau 逼近 goal。dt 与 tau 同单位。
// 帧率无关，dt 很大时直接到达，不会越过。
inline float approach(float v, float goal, float dt, float tau) {
    if (tau <= 0.0f) return goal;
    const float k = 1.0f - std::exp(-dt / tau);
    return v + (goal - v) * k;
}

// 左手系视图矩阵，配合下面的左手系投影。
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 z = normalize(target - eye);
    const Vec3 x = normalize(cross(up, z));
    const Vec3 y = cross(z, x);
    Mat4 r = Mat4::identity();
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
    r.m[3][0] = -dot(x, eye);
    r.m[3][1] = -dot(y, eye);
    r.m[3][2] = -dot(z, eye);
    return r;
}

// 左手系透视投影，深度范围 [0,1]，对应 D3D 的约定。
inline Mat4 perspective(float fovYRad, float aspect, float zn, float zf) {
    const float h = 1.0f / std::tan(fovYRad * 0.5f);
    const float w = h / aspect;
    Mat4 r;
    r.m[0][0] = w;
    r.m[1][1] = h;
    r.m[2][2] = zf / (zf - zn);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
    return r;
}

}  // namespace pet
