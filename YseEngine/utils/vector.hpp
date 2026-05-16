/*
  ==============================================================================

    vector.h
    Created: 29 Jan 2014 11:12:33pm
    Author:  yvan

  ==============================================================================
*/

#ifndef VECTOR_H_INCLUDED
#define VECTOR_H_INCLUDED

#include <string>
#include <sstream>
#include <atomic>
#include <cmath>
#include "../headers/types.hpp"

namespace YSE {
  /// @cond INTERNAL
  class aPos;
  /// @endcond

  /**
   *  @brief 3D position vector — the spatial coordinate used throughout libYSE.
   *
   *  Used for ``sound`` positions, the ``Listener`` position, and ``reverb``
   *  zone centres. Components are plain floats — read or write ``x``, ``y``,
   *  ``z`` directly. Arithmetic and comparison operators behave component-wise.
   */
  class API Pos {
  public:
    /** @brief Cartesian components. */
    float x, y, z;

    /** @brief Set all components to zero. */
    Pos& zero() { x = y = z = 0; return (*this); }

    /** @brief Set all components to ``r``. */
    Pos& set(float r) { x = y = z = r; return (*this); }

    /** @brief Set all three components. */
    Pos& set(float x, float y, float z) { this->x = x, this->y = y, this->z = z; return (*this); }

    /** @brief Euclidean length of the vector. */
    float length() { return (float)sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2)); }

    /** @brief Human-readable ``"X: x Y: y Z: z"`` representation. */
    std::string asText() {
      std::stringstream result(std::stringstream::in | std::stringstream::out);
      result << "X: " << x << " Y: " << y << " Z: " << z;
      return result.str();
    }

    /** @brief Add a scalar to every component. */
    Pos& operator+=(float r) { x += r; y += r; z += r; return (*this); }
    /** @brief Subtract a scalar from every component. */
    Pos& operator-=(float r) { x -= r; y -= r; z -= r; return (*this); }
    /** @brief Multiply every component by a scalar. */
    Pos& operator*=(float r) { x *= r; y *= r; z *= r; return (*this); }
    /** @brief Divide every component by a scalar. */
    Pos& operator/=(float r) { x /= r; y /= r; z /= r; return (*this); }
    /** @brief Component-wise add another vector. */
    Pos& operator+=(const Pos &v) { x += v.x; y += v.y; z += v.z; return (*this); }
    /** @brief Component-wise subtract another vector. */
    Pos& operator-=(const Pos &v) { x -= v.x; y -= v.y; z -= v.z; return (*this); }
    /** @brief Component-wise multiply by another vector. */
    Pos& operator*=(const Pos &v) { x *= v.x; y *= v.y; z *= v.z; return (*this); }
    /** @brief Component-wise divide by another vector. */
    Pos& operator/=(const Pos &v) { x /= v.x; y /= v.y; z /= v.z; return (*this); }
    /** @brief Component-wise equality. */
    bool operator==(const Pos &v) const {
      if (x == v.x && y == v.y && z == v.z) return true;
      return false;
    }
    /** @brief Component-wise inequality. */
    bool operator!=(const Pos &v) const {
      if (x != v.x || y != v.y || z != v.z) return true;
      return false;
    }

    friend Pos  operator+ (const	Pos     &v, float      r) { return Pos(v.x + r, v.y + r, v.z + r); }
    friend Pos  operator- (const	Pos     &v, float      r) { return Pos(v.x - r, v.y - r, v.z - r); }
    friend Pos  operator* (const	Pos     &v, float      r) { return Pos(v.x*r, v.y*r, v.z*r); }
    friend Pos  operator/ (const	Pos     &v, float      r) { return Pos(v.x / r, v.y / r, v.z / r); }
    friend Pos  operator+ (float      r,  const Pos     &v) { return Pos(r + v.x, r + v.y, r + v.z); }
    friend Pos  operator- (float      r,  const Pos     &v) { return Pos(r - v.x, r - v.y, r - v.z); }
    friend Pos  operator* (float      r,  const Pos     &v) { return Pos(r*v.x, r*v.y, r*v.z); }
    friend Pos  operator/ (float      r,  const Pos     &v) { return Pos(r / v.x, r / v.y, r / v.z); }
    friend Pos  operator+ (const	Pos     &a, const Pos     &b) { return Pos(a.x + b.x, a.y + b.y, a.z + b.z); }
    friend Pos  operator- (const	Pos     &a, const Pos     &b) { return Pos(a.x - b.x, a.y - b.y, a.z - b.z); }
    friend Pos  operator* (const	Pos     &a, const Pos     &b) { return Pos(a.x*b.x, a.y*b.y, a.z*b.z); }
    friend Pos  operator/ (const	Pos     &a, const Pos     &b) { return Pos(a.x / b.x, a.y / b.y, a.z / b.z); }

    /** @brief Construct at the origin (0, 0, 0). */
    Pos() {
        set(0.f);
    }
    /** @brief Construct with every component set to ``r``. */
    Pos(float r) { set(r); }
    /** @brief Construct with explicit components. */
    Pos(float x, float y, float z) { set(x, y, z); }
    /** @brief Construct from an atomic position object. */
    Pos(const aPos & v);
  };

  /** @brief Convenience zero-vector instance. */
  static Pos PosEmpty;

  /** @brief Minimum of two integers. */
  inline Int  Min(Int  x, Int  y) { return (x<y) ? x : y; }
  inline Int  Min(UInt x, Int  y) { return (static_cast<Int>(x)<y) ? static_cast<Int>(x) : y; }
  inline Int  Min(Int  x, UInt y) { return (x<static_cast<Int>(y)) ? x : static_cast<Int>(y); }
  inline UInt Min(UInt x, UInt y) { return (x<y) ? x : y; }
  inline I64  Min(Int  x, I64  y) { return (static_cast<I64>(x)<y) ? static_cast<I64>(x) : y; }
  inline Flt  Min(Flt  x, Flt  y) { return (x<y) ? x : y; }
  inline Flt  Min(Int  x, Flt  y) { return (static_cast<Flt>(x)<y) ? static_cast<Flt>(x) : y; }
  inline Flt  Min(Flt  x, Int  y) { return (x<static_cast<Flt>(y)) ? x : static_cast<Flt>(y); }
  inline Dbl  Min(Dbl  x, Dbl  y) { return (x<y) ? x : y; }

  inline Int  Max(Int  x, Int  y) { return (x>y) ? x : y; }
  inline UInt Max(UInt x, Int  y) { return (x>static_cast<UInt>(y)) ? x : static_cast<UInt>(y); }
  inline UInt Max(Int  x, UInt y) { return (static_cast<UInt>(x)>y) ? static_cast<UInt>(x) : y; }
  inline UInt Max(UInt x, UInt y) { return (x>y) ? x : y; }
  inline I64  Max(Int  x, I64  y) { return (static_cast<I64>(x)>y) ? static_cast<I64>(x) : y; }
  inline Flt  Max(Flt  x, Flt  y) { return (x>y) ? x : y; }
  inline Flt  Max(Int  x, Flt  y) { return (static_cast<Flt>(x)>y) ? static_cast<Flt>(x) : y; }
  inline Flt  Max(Flt  x, Int  y) { return (x>static_cast<Flt>(y)) ? x : static_cast<Flt>(y); }
  inline Dbl  Max(Dbl  x, Dbl  y) { return (x>y) ? x : y; }

  /** @brief Component-wise minimum of two positions. */
  inline Pos  Min(const Pos &a, const Pos &b) { return Pos(Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z)); }

  /** @brief Midpoint of two positions. */
  inline Pos  Avg(const Pos &a, const Pos &b) { return (a + b)*0.5f; }

  /** @brief Euclidean distance between two positions. */
  inline float  Dist(const Pos &a, const Pos &b) {
    return sqrt(static_cast<float>(std::pow((b.x - a.x), 2) + std::pow((b.y - a.y), 2) + std::pow((b.z - a.z), 2)));
  }

  /** @brief Dot product of two positions. */
  inline Flt  Dot(const Pos &a, const Pos &b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

  

  
}


#endif  // VECTOR_H_INCLUDED
