#pragma once
#include "pHandle.hpp"

namespace YSE {
  class sound;
  namespace PATCHER {
    class patcherImplementation;
  }

  class API patcher {
  public:
    patcher();
    ~patcher();

    void create(int mainOutputs);

    YSE::pHandle * CreateObject(const std::string & type, const std::string & args = std::string());
    void DeleteObject(YSE::pHandle * obj);
    void Clear();

    void Connect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet);
    void Disconnect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet);

    static bool IsValidObject(const char * type);

    std::string DumpJSON();
    void ParseJSON(const std::string & content);

    // use after loading JSON content
    unsigned int Objects();
    YSE::pHandle * GetHandleFromList(unsigned int obj);
    YSE::pHandle * GetHandleFromID(unsigned int obj);

    // for external data input
    void PassBang(const std::string & to);
    void PassData(int value, const std::string & to);
    void PassData(float value, const std::string & to);
    void PassData(const std::string & value, const std::string & to);


  private: 
    PATCHER::patcherImplementation * pimpl;
    
    friend class YSE::sound;
  };

}