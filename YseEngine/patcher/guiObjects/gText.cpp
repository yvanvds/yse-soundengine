#include "gText.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;
#define className gText

CONSTRUCT() {
  ADD_PARAM(text);

  ADD_DESCRIPTION("Text label. Holds a string parameter for display; no inlets, no outlets — "
                  "purely a visual annotation.");
  ADD_CATEGORY(pCategory::GUI);
  PARAM_DOC("text", "", "Label text.", "any string");
}