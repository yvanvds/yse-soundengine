#include "patcherImplementation.h"
#include "pRegistry.h"
#include "pObjectList.hpp"
#include "headers\enums.hpp"
#include "genericObjects\pDac.h"
#include "pHandle.hpp"
#include "utils\json.hpp"
#include <string>
#include "implementations\logImplementation.h"

using namespace YSE::PATCHER;

patcherImplementation::patcherImplementation(int mainOutputs, YSE::patcher * head)
  : pObject(false) 
  , controlledBySound(false)
  , head(head)
  , fileHandlerActive(false)
{
  output.resize(mainOutputs);
}

patcherImplementation::~patcherImplementation() {
  // memory cleanup
  Clear();
}

const char * patcherImplementation::Type() const {
  return YSE::OBJ::PATCHER;
}

void patcherImplementation::Calculate(YSE::THREAD thread) {
  // works a bit different in case of patchers!
  // only called by the main patcher to generate output
  mtx.lock();

  // invalidate all dsp buffers
  ResetDSP();

  // calculate all objects
  for (const auto& any : objects) {
    if (any.second->IsDSPStartPoint()) {
      any.second->Calculate(thread);
    }
  }

  // clear output 
  for (unsigned int i = 0; i < output.size(); i++) {
    output[i] = 0;
  }

  // sum outputs
  int counter = 0;
  for (const auto& any : objects) {
    if (any.second->Type() == YSE::OBJ::D_DAC) {
      for (unsigned int i = 0; i < output.size(); i++) {
        YSE::DSP::buffer * ptr = ((pDac*)any.second)->GetBuffer(i);
        if (ptr != nullptr) {
          output[i] = *ptr;
        }
      }
      counter++;
    }
  }

  // normalize output 
  if (counter > 1) {
    for (unsigned int i = 0; i < output.size(); i++) {
      output[i] /= (float)counter;
    }
  }

  mtx.unlock();
}

void patcherImplementation::ResetDSP() {
  for (const auto& any : objects) {
    any.second->ResetDSP();
  }
}


void patcherImplementation::Connect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet) {
  if (!fileHandlerActive) mtx.lock();
  PATCHER::outlet * out = from->object->GetOutlet(outlet);
  PATCHER::inlet * in = to->object->GetInlet(inlet);
  from->object->ConnectOutlet(in, outlet);
  to->object->ConnectInlet(out, inlet);
  if (!fileHandlerActive) mtx.unlock();
}

void patcherImplementation::Disconnect(YSE::pHandle * from, int outlet, YSE::pHandle * to, int inlet) {
  if (!fileHandlerActive) mtx.lock();
  to->object->DisconnectInlet(from->object->GetOutlet(outlet), inlet);
  if (!fileHandlerActive) mtx.unlock();
}

YSE::pHandle * patcherImplementation::CreateObject(const std::string & type, const std::string & args) {
  YSE::pHandle * handle = nullptr;
  pObject * object = nullptr;
  INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: Trying to create " + type);
  
  if (type == OBJ::D_DAC) {
    object = new pDac(output.size());
  }
  else {
    object = Register().Get(type);
    if (object != nullptr) {
      object->SetParams(args);
      object->SetParent(this);
    }
  }

  if (object == nullptr) {
    INTERNAL::LogImpl().emit(E_ERROR, "Patcher" + type + " is not a valid patcher object.");
    return nullptr;
  }

  handle = new YSE::pHandle(object);
  
  if (!fileHandlerActive) mtx.lock();
  objects.insert(std::pair<YSE::pHandle*, pObject*>(handle, object));
  if (!fileHandlerActive) mtx.unlock();
  INTERNAL::LogImpl().emit(E_DEBUG, "Patcher: " + type + " created");
  return handle;
}

void patcherImplementation::DeleteObject(YSE::pHandle * handle) {
  if(!fileHandlerActive) mtx.lock();
  objects.erase(handle);

  delete handle->object;
  delete handle;
  if (!fileHandlerActive) mtx.unlock();
}

void patcherImplementation::Clear() {
  for (auto it = objects.begin(); it != objects.end(); ++it) {
    delete it->first;
    delete it->second;
  }
}

using json = nlohmann::json;
std::string patcherImplementation::DumpJSON() {
  json j;
  int counter = 0;

  mtx.lock();
  fileHandlerActive = true;
  for (const auto& any : objects) {
    std::string name = "object " + std::to_string(counter);
    any.second->DumpJson(j[name]);
    counter++;
  }
  fileHandlerActive = false;
  mtx.unlock();

  std::string result = j.dump(2, ' ', true);
  return result;
}

void patcherImplementation::ParseJSON(const std::string & content) {
  auto j = json::parse(content);

  std::map<int, pHandle*> OldIDs;

  mtx.lock();
  fileHandlerActive = true;
  // restore objects first
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    std::string type = obj.value()["type"].get<std::string>();
    std::string args = obj.value()["parms"].get<std::string>();
    pHandle * handle = CreateObject(type, args);

    // handle can be null if called without gui context
    if (handle != nullptr) {
      auto gui = obj.value()["gui"];
      for (auto prop = gui.begin(); prop != gui.end(); ++prop) {
        handle->SetGuiProperty(prop.key(), prop.value().get<std::string>());
      }
    }

    int ID = obj.value()["ID"].get<int>();
    OldIDs.insert(std::pair<int, YSE::pHandle*>(ID, handle));
  }

  // restore connections
  for (auto obj = j.begin(); obj != j.end(); ++obj) {
    int source = obj.value()["ID"].get<int>();
    int outlet = 0;
    auto outs = obj.value()["outputs"];
    for (auto out = outs.begin(); out != outs.end(); ++out) {
      
      if (out.value().count("Count") == 0) {
        continue;
      }
      int count = out.value()["Count"].get<int>();

      for (int i = 0; i < count; i++) {
        auto connection = out.value()[std::to_string(i)];
        int target = connection["Object"].get<int>();
        int inlet = connection["Inlet"].get<int>();

        pHandle * sourceHandle = nullptr;
        pHandle * targetHandle = nullptr;

        auto a = OldIDs.find(source);
        if (a != OldIDs.end()) {
          sourceHandle = a->second;
        }

        auto b = OldIDs.find(target);
        if (b != OldIDs.end()) {
          targetHandle = b->second;
        }

        if (targetHandle != nullptr && sourceHandle != nullptr) {
          Connect(sourceHandle, outlet, targetHandle, inlet);
        }
      }
      outlet++;
    }
  }
  fileHandlerActive = false;
  mtx.unlock();
}

unsigned int patcherImplementation::Objects() {
  return objects.size();
}

YSE::pHandle * patcherImplementation::GetHandleFromList(unsigned int obj) {
  // TODO: not really brilliant, this code
  unsigned int pos = 0;
  for (auto& x : objects) {
    if (pos == obj) return x.first;
    pos++;
  }
  return 0;
}

YSE::pHandle * patcherImplementation::GetHandleFromID(unsigned int objID) {
  for (auto& x : objects) {
    if (x.second->GetID() == objID) return x.first;
  }
  return nullptr;
} 

void patcherImplementation::PassBang(const std::string & to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (x.second->Type() == OBJ::G_RECEIVE) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetBang(thread);
      }
    }
  }
}

void patcherImplementation::PassData(int value, const std::string & to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (x.second->Type() == OBJ::G_RECEIVE) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetInt(value, thread);
      }
    }
  }
}

void patcherImplementation::PassData(float value, const std::string & to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (x.second->Type() == OBJ::G_RECEIVE) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetFloat(value, thread);
      }
    }
  }
}

void patcherImplementation::PassData(const std::string & value, const std::string & to, YSE::THREAD thread) {
  for (auto& x : objects) {
    if (x.second->Type() == OBJ::G_RECEIVE) {
      if (to.compare(x.second->DataName()) == 0) {
        x.second->GetInlet(0)->SetList(value, thread);
      }
    }
  }
}