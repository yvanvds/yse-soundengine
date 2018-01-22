#include "parameters.h"

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
  size_t pos = 0;
  unsigned int currentArg = 0;
  std::string token;
  while ((pos = args.find(" ")) != std::string::npos) {
    token = args.substr(0, pos);

    if (currentArg < parms.size()) {
      switch (parms[currentArg].type) {
        case FLOAT: {
          float f = std::stof(token);
          *((float*)parms[currentArg].value) = f;
          break;
        }
        case ATOMIC_FLOAT: {
          float f = std::stof(token);
          *((std::atomic<float>*)parms[currentArg].value) = f;
          break;
        }
        case INT: {
          int i = std::stoi(token);
          *((int*)parms[currentArg].value) = i;
          break;
        }
        case ATOMIC_INT: {
          int i = std::stoi(token);
          *((std::atomic<int>*)parms[currentArg].value) = i;
          break;
        }
        case STRING: {
          *((std::string*)parms[currentArg].value) = token;
        }
      }
    }

    currentArg++;
  }
}