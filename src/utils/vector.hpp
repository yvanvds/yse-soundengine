#pragma once
#include <string>

namespace YSE {



	struct API Vec {
#pragma warning(disable : 4018)
		Flt x, y, z;

		Vec& zero() {x=y=z=0; return (*this);}
		Vec& set(Flt r) {x=y=z=r; return (*this);}
		Vec& set(Flt x, Flt y, Flt z) { this->x = x, this->y = y, this->z = z; return (*this);}
		Flt length();
		std::string asText();

		Vec& operator+=(			Flt	 r) {x+=  r; y+=  r; z+=  r; return (*this);}
    Vec& operator-=(			Flt  r) {x-=  r; y-=  r; z-=  r; return (*this);}
    Vec& operator*=(			Flt  r) {x*=  r; y*=  r; z*=  r; return (*this);}
    Vec& operator/=(			Flt  r) {x/=  r; y/=  r; z/=  r; return (*this);}
    Vec& operator+=(const Vec &v) {x+=v.x; y+=v.y; z+=v.z; return (*this);}
    Vec& operator-=(const Vec &v) {x-=v.x; y-=v.y; z-=v.z; return (*this);}
    Vec& operator*=(const Vec &v) {x*=v.x; y*=v.y; z*=v.z; return (*this);}
    Vec& operator/=(const Vec &v) {x/=v.x; y/=v.y; z/=v.z; return (*this);}
    Bool operator==(const Vec &v) const;
    Bool operator!=(const Vec &v) const;

		friend Vec  operator+ (const	Vec     &v,				Flt      r) {return Vec(v.x+r, v.y+r, v.z+r);}
		friend Vec  operator- (const	Vec     &v,				Flt      r) {return Vec(v.x-r, v.y-r, v.z-r);}
		friend Vec  operator* (const	Vec     &v,				Flt      r) {return Vec(v.x*r, v.y*r, v.z*r);}
		friend Vec  operator/ (const	Vec     &v,				Flt      r) {return Vec(v.x/r, v.y/r, v.z/r);}
		friend Vec  operator+ (				Flt      r, const Vec     &v) {return Vec(r+v.x, r+v.y, r+v.z);}
		friend Vec  operator- (				Flt      r, const Vec     &v) {return Vec(r-v.x, r-v.y, r-v.z);}
		friend Vec  operator* (				Flt      r, const Vec     &v) {return Vec(r*v.x, r*v.y, r*v.z);}
		friend Vec  operator/ (				Flt      r, const Vec     &v) {return Vec(r/v.x, r/v.y, r/v.z);}
		friend Vec  operator+ (const	Vec     &a, const Vec     &b) {return Vec(a.x+b.x, a.y+b.y, a.z+b.z);}
		friend Vec  operator- (const	Vec     &a, const Vec     &b) {return Vec(a.x-b.x, a.y-b.y, a.z-b.z);}
		friend Vec  operator* (const	Vec     &a, const Vec     &b) {return Vec(a.x*b.x, a.y*b.y, a.z*b.z);}
		friend Vec  operator/ (const	Vec     &a, const Vec     &b) {return Vec(a.x/b.x, a.y/b.y, a.z/b.z);}

		Vec() {}
		//Vec(Vec& v) {x=v.x; y=v.y; z=v.z;}
		Vec(Flt r) {set(r);}
		Vec(Flt x, Flt y, Flt z) {set(x, y, z);}

	};

	// get minimum/maximum value
	inline Int  Min(Int  x, Int  y) {return (x<y) ? x : y;}
	inline Int  Min(UInt x, Int  y) {return (x<y) ? x : y;}
	inline Int  Min(Int  x, UInt y) {return (x<y) ? x : y;}
	inline UInt Min(UInt x, UInt y) {return (x<y) ? x : y;}
	inline I64  Min(Int  x, I64  y) {return (x<y) ? x : y;}
	inline Flt  Min(Flt  x, Flt  y) {return (x<y) ? x : y;}
	inline Flt  Min(Int  x, Flt  y) {return (x<y) ? x : y;}
	inline Flt  Min(Flt  x, Int  y) {return (x<y) ? x : y;}
	inline Dbl  Min(Dbl  x, Dbl  y) {return (x<y) ? x : y;}

	inline Int  Max(Int  x, Int  y) {return (x>y) ? x : y;}
	inline UInt Max(UInt x, Int  y) {return (x>y) ? x : y;}
	inline UInt Max(Int  x, UInt y) {return (x>y) ? x : y;}
	inline UInt Max(UInt x, UInt y) {return (x>y) ? x : y;}
	inline I64  Max(Int  x, I64  y) {return (x>y) ? x : y;}
	inline Flt  Max(Flt  x, Flt  y) {return (x>y) ? x : y;}
	inline Flt  Max(Int  x, Flt  y) {return (x>y) ? x : y;}
	inline Flt  Max(Flt  x, Int  y) {return (x>y) ? x : y;}
	inline Dbl  Max(Dbl  x, Dbl  y) {return (x>y) ? x : y;}

	inline Vec  Min  (const Vec &a, const Vec &b) {return Vec(Min(a.x,b.x), Min(a.y,b.y), Min(a.z,b.z));}
	inline Vec  Avg  (const Vec &a, const Vec &b) {return (a+b    )*0.5f ;}
				 Flt  Dist (const Vec &a, const Vec &b);
	inline Flt  Dot  (const Vec &a, const Vec &b) {return a.x*b.x + a.y*b.y + a.z*b.z ;}
				 Vec  Cross(const Vec &a, const Vec &b);

}

