#pragma once
#include "dsp/buffer.hpp"
#include <functional>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct outlet;

    typedef std::function<void(float, int)> floatFunc;
    typedef std::function<void(int)> voidFunc;
    typedef std::function<void(DSP::buffer * buffer, int)> bufferFunc;

    struct inlet {
      inlet(pObject * obj, bool active, int position);
      ~inlet();

      void RegisterBang(voidFunc f);
      void RegisterFloat(floatFunc f);
      void RegisterBuffer(bufferFunc f);
      
      void SetBang();
      void SetFloat(float value);
      void SetBuffer(DSP::buffer * value);

      void SetMessage(const std::string & message, float value = 0.f);

      inline void ResetDSP() { dspReady = false; } // call before parsing patcher objects
      bool WaitingForDSP() const; // true when no dsp buffer has been received during this frame
      bool HasActiveDSPConnection() const;
      bool AcceptsDSP() const;

      bool Connect(outlet * out);
      void Disconnect(outlet * out);

      int GetObjectID();
      int GetPosition();

    private:
      pObject * obj;
      bool dspReady;
      bool active;
      int position;

      voidFunc onBang;
      floatFunc onFloat;
      bufferFunc onBuffer;

      outlet * dspConnection;
      std::vector<outlet*> connections;
    };
  }
}