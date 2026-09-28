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

/** @file
 *  @brief Fixed-size type aliases used throughout the public API.
 *
 *  They live in the global namespace. ``Flt`` (float), ``Dbl`` (double),
 *  ``Int`` and ``UInt`` (32-bit) appear in most signatures.
 */

/* please use these variable types, at the very least within the library.
They make it easier to implement platform independent code later on.
*/

typedef bool Bool; ///< ``bool``.
typedef char Char8; ///< ``char``.
typedef float Flt; ///< 32-bit float.
typedef double Dbl; ///< 64-bit float.
typedef PLATFORM(signed __int8, int8_t) I8; ///< Signed 8-bit integer.
typedef PLATFORM(unsigned __int8, uint8_t) U8, Byte; ///< Unsigned 8-bit integer.
typedef PLATFORM(signed __int16, int16_t) I16, Short; ///< Signed 16-bit integer.
typedef PLATFORM(unsigned __int16, uint16_t) U16, UShort; ///< Unsigned 16-bit integer.
typedef PLATFORM(signed __int32, int32_t) I32, Int; ///< Signed 32-bit integer.
typedef PLATFORM(unsigned __int32, uint32_t) U32, UInt; ///< Unsigned 32-bit integer.
typedef PLATFORM(signed __int64, long long) I64, Long; ///< Signed 64-bit integer.
typedef PLATFORM(unsigned __int64, uint64_t) U64, ULong; ///< Unsigned 64-bit integer.

// thread safe versions of the most used variable types (a stands for atomic)
typedef std::atomic<Bool> aBool; ///< Atomic ``Bool``.
typedef std::atomic<Int> aInt; ///< Atomic ``Int``.
typedef std::atomic<UInt> aUInt; ///< Atomic ``UInt``.
typedef std::atomic<Flt> aFlt; ///< Atomic ``Flt``.
typedef std::atomic<Byte> aByte; ///< Atomic ``Byte``.

/// @cond INTERNAL
// shorthand macro for iterating a container object. Can be used if
// the container has a size function. All containers within YSE should
// be constructed so that this function works.
#define FOREACH(T) for (UInt i = 0; i < T.size(); i++)
#define FOREACH_D(D, T) for (UInt D = 0; D < T.size(); D++)
/// @endcond

#endif // TYPES_HPP_INCLUDED
