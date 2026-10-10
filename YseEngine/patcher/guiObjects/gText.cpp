#include "gText.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gText

CONSTRUCT() {
  ADD_PARAM(text);

  ADD_DESCRIPTION("A comment in the patch. It holds its text for a host to display and has no "
                  "inlets or outlets.");
  ADD_CATEGORY(pCategory::GUI);
  PARAM_DOC("text", "", "The text to display.", "any text");
}