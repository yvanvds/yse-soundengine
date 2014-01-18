#pragma once
#include <stdlib.h>

namespace YSE {
  template<typename Ty>  Ty* Alloc(UInt elms = 1) {
	  Ty * buffer;
	  buffer = (Ty*) malloc(elms * sizeof(Ty));
	  return buffer;
  }

  template<typename Ty> void Free(Ty* &data) {
	  free(data);
  }

  template<typename Ty> void Realloc(Ty* &data, UInt newSize) {
	  data = (Ty*) realloc(data, newSize * sizeof(Ty));
  }

  template<typename Ty> void ReallocZero(Ty ** data, UInt current, UInt newSize) {
	  *data = (Ty*) realloc(*data, newSize * sizeof(Ty));
	  for (;current < newSize; current++) {
		  (*data)[current] = (Ty)0;
	  }
  }

}