#include "parameters.h"
#include "implementations\logImplementation.h"
using namespace YSE::PATCHER;

void Parameters::Register(int & value) {
  parms.emplace_back(PARM_TYPE::INT, &value);
}

void Parameters::Register(std::atomic<int> & value) {
  parms.emplace_back(PARM_TYPE::ATOMIC_INT, &value);
}

void Parameters::Register(float & value) {
  parms.emplace_back(PARM_TYPE::FLOAT, &value);
}

void Parameters::Register(std::atomic<float> & value) {
  parms.emplace_back(PARM_TYPE::ATOMIC_FLOAT, &value);
}

void Parameters::Register(std::string & value) {
  parms.emplace_back(PARM_TYPE::STRING, &value);
}

const std::string & Parameters::Get() {
  return current;
}

void Parameters::Set(const std::string & args) {
  if (args.size() == 0) return;

  INTERNAL::LogImpl().emit(E_DEBUG, "patcher: parsing arguments: " + args);
  size_t pos = 0;
  unsigned int currentArg = 0;
  std::string arg = args + " "; // important to get the last argument
  std::string token;
  while ((pos = arg.find(" ")) != std::string::npos) {
    token = arg.substr(0, pos);
    INTERNAL::LogImpl().emit(E_DEBUG, "patcher: found argument: " + args);

    if (currentArg < parms.size()) {
      switch (parms[currentArg].type) {
        case FLOAT: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Using argument as float.");
          float f = std::stof(token);
          *((float*)parms[currentArg].value) = f;
          break;
        }
        case ATOMIC_FLOAT: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Using argument as atomic float.");
          float f = std::stof(token);
          *((std::atomic<float>*)parms[currentArg].value) = f;
          break;
        }
        case INT: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Using argument as int.");
          int i = std::stoi(token);
          *((int*)parms[currentArg].value) = i;
          break;
        }
        case ATOMIC_INT: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Using argument as atomic int.");
          int i = std::stoi(token);
          *((std::atomic<int>*)parms[currentArg].value) = i;
          break;
        }
        case STRING: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Using argument as string.");
          *((std::string*)parms[currentArg].value) = token;
        }
        default: {
          INTERNAL::LogImpl().emit(E_DEBUG, "Argument type unknown.");
        }
      }
    
    }
    else {
      INTERNAL::LogImpl().emit(E_DEBUG, "Too many arguments for this object.");
    }
    arg.erase(0, pos + 1);
    currentArg++;
  }
}