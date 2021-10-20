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
  class aPos;
  
  class API Pos {
  public:
    float x, y, z;

    Pos& zero() { x = y = z = 0; return (*this); }
    Pos& set(float r) { x = y = z = r; return (*this); }
    Pos& set(float x, float y, float z) { this->x = x, this->y = y, this->z = z; return (*this); }
    float length() { return sqrt(std::pow(x, 2) + std::pow(y, 2) + std::pow(z, 2)); }
    const char * asText() {
      std::stringstream result(std::stringstream::in | std::stringstream::out);
      result << "X: " << x << " Y: " << y << " Z: " << z;
      return result.str().c_str();
    }

    Pos& operator+=(float r) { x += r; y += r; z += r; return (*this); }
    Pos& operator-=(float r) { x -= r; y -= r; z -= r; return (*this); }
    Pos& operator*=(float r) { x *= r; y *= r; z *= r; return (*this); }
    Pos& operator/=(float r) { x /= r; y /= r; z /= r; return (*this); }
    Pos& operator+=(const Pos &v) { x += v.x; y += v.y; z += v.z; return (*this); }
    Pos& operator-=(const Pos &v) { x -= v.x; y -= v.y; z -= v.z; return (*this); }
    Pos& operator*=(const Pos &v) { x *= v.x; y *= v.y; z *= v.z; return (*this); }
    Pos& operator/=(const Pos &v) { x /= v.x; y /= v.y; z /= v.z; return (*this); }
    bool operator==(const Pos &v) const {
      if (x == v.x && y == v.y && z == v.z) return true;
      return false;
    }
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

    Pos() {}
    //Pos(Pos& v) {x=v.x; y=v.y; z=v.z;}
    Pos(float r) { set(r); }
    Pos(float x, float y, float z) { set(x, y, z); }
    Pos(const aPos & v);
  };

  static Pos PosEmpty;

  // get minimum/maximum value
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

  inline Pos  Min(const Pos &a, const Pos &b) { return Pos(Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z)); }
  inline Pos  Avg(const Pos &a, const Pos &b) { return (a + b)*0.5f; }
  inline float  Dist(const Pos &a, const Pos &b) {
    return sqrt(std::pow((b.x - a.x), 2) + std::pow((b.y - a.y), 2) + std::pow((b.z - a.z), 2));
  }
  inline Flt  Dot(const Pos &a, const Pos &b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
  //Pos Cross(const Pos &a, const Pos &b);

  

  
}


#endif  // VECTOR_H_INCLUDED
