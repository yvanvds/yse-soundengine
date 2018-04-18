#include "string.h"
#include "headers\defines.hpp"

#ifdef YSE_ANDROID
#include <sstream>

#define SSTR( x ) static_cast< std::ostringstream & >( \
        ( std::ostringstream() << std::dec << x ) ).str()
#endif


std::string YSE::ToString(int value)
{
#ifdef YSE_ANDROID
  return SSTR(value);
#else
  return std::to_string(value);
#endif
}
