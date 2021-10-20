/*
  ==============================================================================

    types.hpp
    Created: 27 Jan 2014 7:16:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef TYPES_HPP_INCLUDED
#define TYPES_HPP_INCLUDED

#include "defines.hpp"
#include <atomic>

/* please use these variable types, at the very least within the library.
They make it easier to implement platform independent code later on.
*/

typedef bool                                 Bool ;
typedef char                                 Char8;
typedef wchar_t                              Char ;
typedef float                                Flt  ;
typedef double                               Dbl  ;
typedef PLATFORM(signed	__int8   , int8_t  ) I8   ;
typedef PLATFORM(unsigned	__int8 , uint8_t ) U8 , Byte  ;
typedef PLATFORM(signed	__int16  , int16_t ) I16, Short ;
typedef PLATFORM(unsigned	__int16, uint16_t) U16, UShort;
typedef PLATFORM(signed	__int32  , int32_t ) I32, Int   ;
typedef PLATFORM(unsigned	__int32, uint32_t) U32, UInt  ;
typedef PLATFORM(signed	__int64  , long long ) I64, Long  ;
typedef PLATFORM(unsigned	__int64, uint64_t) U64, ULong ;


// thread safe versions of the most used variable types (a stands for atomic)
typedef std::atomic<Bool> aBool;
typedef std::atomic<Int > aInt ;
typedef std::atomic<UInt> aUInt;
typedef std::atomic<Flt > aFlt ;
typedef std::atomic<Byte> aByte;

// shorthand macro for iterating a container object. Can be used if 
// the container has a size function. All containers within YSE should
// be constructed so that this function works.
#define FOREACH(T) for (UInt i = 0; i < T.size(); i++)
#define FOREACH_D(D, T) for (UInt D = 0; D < T.size(); D++)

#endif  // TYPES_HPP_INCLUDED
