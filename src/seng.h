#include <string>

typedef						bool		Bool			;
typedef						char		Char8			;
typedef						wchar_t Char			;
typedef signed		__int8	I8				;
typedef unsigned	__int8	U8	,	Byte;
typedef signed		__int16 I16				;
typedef unsigned	__int16 U16				;
typedef signed		__int32 I32	, Int	;
typedef unsigned	__int32 U32	, UInt;
typedef signed		__int64 I64				;
typedef unsigned	__int64 U64				;
typedef						float		Flt				;
typedef						double	Dbl				;

namespace SENG {
	
	struct system {
		void update();
		void close();
	};

	extern system System;


	struct Vec {
		Flt x, y, z;

		Vec& zero() {x=y=z=0; return (*this);}
		Vec& set(Flt r) {x=y=z=r; return (*this);}
		Vec& set(Flt x, Flt y, Flt z) { this->x = x, this->y = y, this->z = z; return (*this);}
		Flt length();

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

	struct sound {
		void pos(const Vec &v) { _pos = v; }
		Vec pos() { return _pos; }

		Bool create(std::string &fileName);
		void initialize(); // sets variables and asks memory. Use this when create returns true;
		
		void play();
		void pause();
		void stop();
		void toggle(); // switch between play & pause

		~sound();
	private:
		Vec _pos;
	};

	sound * AddSound(std::string fileName);
}