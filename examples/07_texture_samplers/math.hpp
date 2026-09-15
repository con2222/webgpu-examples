#ifndef MATH_HPP
#define MATH_HPP

#include <cmath>

// ------------------------------------------------------------
// vec2
// ------------------------------------------------------------

struct vec2 {
    union {
        struct {
            float x, y;
        };

        float data[2];
    };

    vec2() : data{0.0f, 0.0f} {}
    vec2(float _x, float _y) : x(_x), y(_y) {}
};

// ------------------------------------------------------------
// vec4
// ------------------------------------------------------------

struct vec4 {
    union {
        struct {
            float x, y, z, w;
        };

        float data[4];
    };

    vec4() : data{0.0f, 0.0f, 0.0f, 0.0f} {}

    vec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

    float& operator[](const int idx) { return data[idx]; }

    const float& operator[](const int idx) const { return data[idx]; }

    vec4& operator+=(const vec4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;

        return *this;
    }

    vec4& operator-=(const vec4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;

        return *this;
    }

    vec4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;

        return *this;
    }

    vec4& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;

        return *this;
    }
};

// ------------------------------------------------------------
// vec4 arithmetic
// ------------------------------------------------------------

inline vec4 operator+(const vec4& a, const vec4& b) {
    return vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

inline vec4 operator-(const vec4& a, const vec4& b) {
    return vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

inline vec4 operator-(const vec4& v) { return vec4{-v.x, -v.y, -v.z, -v.w}; }

inline vec4 operator*(const vec4& v, float scalar) {
    return vec4{v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar};
}

inline vec4 operator*(float scalar, const vec4& v) { return v * scalar; }

inline vec4 operator/(const vec4& v, float scalar) {
    return vec4{v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar};
}

// ------------------------------------------------------------
// Vector math
// ------------------------------------------------------------

// Dot product:
//
// a · b = ax*bx + ay*by + az*bz + aw*bw
//
inline float dot(const vec4& a, const vec4& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// Squared length.
//
// Useful when you only need to compare lengths,
// because sqrt() is not required.
//
inline float length_squared(const vec4& v) { return dot(v, v); }

// Vector length:
//
// |v| = sqrt(v · v)
//
inline float length(const vec4& v) { return std::sqrt(length_squared(v)); }

// Normalize vector.
//
// Makes vector length equal to 1.
//
// Important:
// usually you normalize direction vectors,
// for which w = 0.
//
inline vec4 normalize(const vec4& v) {
    float len = length(v);

    if (len < 0.000001f) {
        return vec4{};
    }

    return v / len;
}

// Cross product.
//
// Cross product only exists for 3D vectors,
// so x/y/z are used and w becomes 0.
//
// Result is perpendicular to both a and b.
//
inline vec4 cross(const vec4& a, const vec4& b) {
    return vec4{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x, 0.0f};
}

// ------------------------------------------------------------
// mat4
// ------------------------------------------------------------

// Column-major.
//
// cols[0] = first column
// cols[1] = second column
// ...
//
// Matrix:
//
// | cols[0][0] cols[1][0] cols[2][0] cols[3][0] |
// | cols[0][1] cols[1][1] cols[2][1] cols[3][1] |
// | cols[0][2] cols[1][2] cols[2][2] cols[3][2] |
// | cols[0][3] cols[1][3] cols[2][3] cols[3][3] |
//
struct mat4 {
    vec4 cols[4];

    mat4() = default;

    mat4(vec4 c0, vec4 c1, vec4 c2, vec4 c3) : cols{c0, c1, c2, c3} {}

    vec4& operator[](const int idx) { return cols[idx]; }

    const vec4& operator[](const int idx) const { return cols[idx]; }

    static mat4 identity();

    static mat4 make_scale(float sx, float sy, float sz);

    static mat4 make_translation(float tx, float ty, float tz);

    static mat4 make_rotation_x(float angle);

    static mat4 make_rotation_y(float angle);

    static mat4 make_rotation_z(float angle);

    static mat4 make_perspective(float fov, float aspect, float znear,
                                 float zfar);
};

// ------------------------------------------------------------
// Matrix constructors
// ------------------------------------------------------------

inline mat4 mat4::identity() {
    mat4 m;

    m[0][0] = 1.0f;
    m[1][1] = 1.0f;
    m[2][2] = 1.0f;
    m[3][3] = 1.0f;

    return m;
}

inline mat4 mat4::make_scale(float sx, float sy, float sz) {
    mat4 m = mat4::identity();

    m[0][0] = sx;
    m[1][1] = sy;
    m[2][2] = sz;

    return m;
}

inline mat4 mat4::make_translation(float tx, float ty, float tz) {
    mat4 m = mat4::identity();

    m[3][0] = tx;
    m[3][1] = ty;
    m[3][2] = tz;

    return m;
}

inline mat4 mat4::make_rotation_x(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    mat4 m = mat4::identity();

    m[1][1] = c;
    m[1][2] = s;

    m[2][1] = -s;
    m[2][2] = c;

    return m;
}

inline mat4 mat4::make_rotation_y(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    mat4 m = mat4::identity();

    m[0][0] = c;
    m[0][2] = s;

    m[2][0] = -s;
    m[2][2] = c;

    return m;
}

inline mat4 mat4::make_rotation_z(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    mat4 m = mat4::identity();

    m[0][0] = c;
    m[0][1] = s;

    m[1][0] = -s;
    m[1][1] = c;

    return m;
}

inline mat4 mat4::make_perspective(float fov, float aspect, float znear,
                                   float zfar) {
    mat4 m = mat4::identity();
    m[0][0] = 1 / (aspect * std::tan(fov / 2));
    m[1][1] = 1 / tan(fov / 2);
    m[2][2] = zfar / (znear - zfar);
    m[2][3] = -1;
    m[3][2] = (znear * zfar) / (znear - zfar);
    return m;
}

// ------------------------------------------------------------
// Matrix arithmetic
// ------------------------------------------------------------

inline mat4 operator+(const mat4& a, const mat4& b) {
    mat4 result;

    for (int i = 0; i < 4; ++i) {
        result[i] = a[i] + b[i];
    }

    return result;
}

inline mat4 operator-(const mat4& a, const mat4& b) {
    mat4 result;

    for (int i = 0; i < 4; ++i) {
        result[i] = a[i] - b[i];
    }

    return result;
}

inline mat4 operator*(const mat4& m, float scalar) {
    mat4 result;

    for (int i = 0; i < 4; ++i) {
        result[i] = m[i] * scalar;
    }

    return result;
}

inline mat4 operator*(float scalar, const mat4& m) { return m * scalar; }

// ------------------------------------------------------------
// Matrix * vector
// ------------------------------------------------------------
//
// Since the matrix is stored by columns:
//
// M * v =
//     column0 * v.x +
//     column1 * v.y +
//     column2 * v.z +
//     column3 * v.w
//
inline vec4 operator*(const mat4& m, const vec4& v) {
    return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w;
}

// ------------------------------------------------------------
// Matrix * matrix
// ------------------------------------------------------------
//
// C = A * B
//
// Each column of C is:
//
// C[i] = A * B[i]
//
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 result;

    result[0] = a * b[0];
    result[1] = a * b[1];
    result[2] = a * b[2];
    result[3] = a * b[3];

    return result;
}

// ------------------------------------------------------------
// Transpose
// ------------------------------------------------------------
//
// Rows become columns.
//
inline mat4 transpose(const mat4& m) {
    mat4 result;

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result[col][row] = m[row][col];
        }
    }

    return result;
}

// ------------------------------------------------------------
// Angle helpers
// ------------------------------------------------------------

constexpr float PI = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * PI / 180.0f; }

#endif  // MATH_HPP