#pragma once

namespace YSE {
  
  namespace PATCHER {
    enum PARM_TYPE {
      PARM_UNDEFINED,
      PARM_INTEGER,
      PARM_FLOAT,
    };

    enum PIN_TYPE {
      PIN_INVALID = 1 << 0,
      PIN_BOOL = 1 << 1,
      PIN_FLOAT = 1 << 2,
      PIN_INT = 1 << 3,
      PIN_STRING = 1 << 4,
      PIN_DSP_BUFFER = 1 << 5
    };

    
  }

}