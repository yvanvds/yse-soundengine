#pragma once
#include "../dsp/buffer.hpp"
#include <functional>
#include <string>
#include <vector>
#include "../headers\enums.hpp"

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct outlet;

    typedef std::function<void(float, int, THREAD)> floatFunc;
    typedef std::function<void(int, int, THREAD)> intFunc;
    typedef std::function<void(int, THREAD)> voidFunc;
    typedef std::function<void(const std::string &, int, THREAD)> listFunc;
    typedef std::function<void(DSP::buffer * buffer, int, THREAD)> bufferFunc;

    struct inlet {
      inlet(pObject * obj, bool active, int position);
      ~inlet();

      void RegisterBang(voidFunc f);
      void RegisterFloat(floatFunc f);
      void RegisterInt(intFunc f);
      void RegisterList(listFunc f);
      void RegisterBuffer(bufferFunc f);
      
      void SetBang(THREAD thread);
      void SetInt(int value, THREAD thread);
      void SetFloat(float value, THREAD thread);
      void SetList(const std::string & value, THREAD thread);
      void SetBuffer(DSP::buffer * value, THREAD thread);

      void SetMessage(const std::string & message, THREAD thread, float value = 0.f);

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

      intFunc onInt;
      voidFunc onBang;
      floatFunc onFloat;
      listFunc onList;
      bufferFunc onBuffer;

      outlet * dspConnection;
      std::vector<outlet*> connections;
    };
  }
}