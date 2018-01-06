#pragma once
#include "dsp/buffer.hpp"
#include "pEnums.h"
#include <vector>

namespace YSE {
  namespace PATCHER {

    enum PIN_DIR {
      IN,
      OUT,
    };

    class pObject;

    struct pin {
      pin(PIN_DIR dir, int position, pObject * thisObject);

      inline void ResetData() { dataIsReady = false; }
      inline bool HasData() { return dataIsReady; }

      void SetData(bool data);
      void SetData(int data);
      void SetData(float data);
      void SetData(const char * data);
      void SetData(DSP::buffer * data);
      inline PIN_TYPE GetCurentDataType() { return currentDataType; }
    protected:
      PIN_DIR direction;
      int position;

      pObject * thisObject;

      bool dataIsReady;
      
      // data
      union {
        bool vBool;
        float vFloat;
        int vInt;
        const char * vString;
        DSP::buffer * vBuffer;
      };

      PIN_TYPE currentDataType;
    };

    struct pinOut;

    struct pinIn : public pin {
      pinIn(int allowedTypes, int position, pObject * thisObject);
      
      bool Accepts(PIN_TYPE type);
      inline bool IsConnected() { return connection != nullptr; }
      void Connect(pinOut * pin);
      void Disconnect();

      void RequestData();
      inline bool GetBool() const { return vBool; }
      inline int GetInt() const { return vInt; }
      inline float GetFloat() const { return vFloat; }
      inline const char * GetString() const { return vString; }
      inline DSP::buffer * GetBuffer() const { return vBuffer; }

      int allowedTypes;
      pinOut * connection;
    };

    struct pinOut : public pin {
      pinOut(PIN_TYPE type, int position, pObject * thisObject);

      bool IsConnected();
      bool HasConnection(pinIn * input);
      bool Connect(pinIn * input);
      void DisConnect(pinIn * input);

      void RequestData(pinIn * input);
      void SendData(pinIn * input);

      void SetData(pinIn * source);
      using pin::SetData;

      PIN_TYPE type;
      std::vector<pinIn*> connections;
    };
  }
}