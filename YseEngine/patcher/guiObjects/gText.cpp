#include "gText.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gText

CONSTRUCT() {
  ADD_PARAM(text);
}