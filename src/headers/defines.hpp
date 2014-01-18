#pragma once
#include <atomic>
#include "types.hpp"


/* please use these variable types, at the very least within the library.
   They make it easier to implement platform independent code later on.
*/

typedef                               bool          Bool	;
typedef                               char          Char8	;
typedef                               wchar_t       Char	;
typedef PLATFORM(   signed	__int8 ,   int8_t)  I8        ;
typedef PLATFORM( unsigned	__int8 ,  uint8_t)  U8,	Byte  ;
typedef PLATFORM(   signed	__int16,  int16_t) I16, Short ;
typedef PLATFORM( unsigned	__int16, uint16_t) U16, UShort;
typedef PLATFORM(   signed	__int32,  int32_t) I32, Int   ;
typedef PLATFORM( unsigned	__int32, uint32_t) U32, UInt  ;
typedef PLATFORM(   signed	__int64,  int64_t) I64, Long	;
typedef PLATFORM( unsigned	__int64, uint64_t) U64, ULong	;
typedef                               float         Flt		;
typedef                               double        Dbl		;

// thread safe versions of the most used variable types (a stands for atomic)
typedef std::atomic<Bool> aBool;
typedef std::atomic<Int>  aInt ;
typedef std::atomic<UInt> aUInt;
typedef std::atomic<Flt>  aFlt ;
typedef std::atomic<Byte> aByte;

// shorthand macro for iterating a container object. Can be used if 
// the container has a size function. All containers within YSE should
// be constructed so that this function works.
#define FOR(T) for (Int i = 0; i < T.size(); i++)
#define FORD(D, T) for (Int D = 0; D < T.size(); D++)

