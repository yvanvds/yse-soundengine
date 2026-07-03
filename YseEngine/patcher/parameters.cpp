
#include "parameters.h"
#include "../implementations/logImplementation.h"

using namespace YSE::PATCHER;

void Parameters::Register(int& value) {
  parms.emplace_back(PARM_TYPE::INT, &value);
}

void Parameters::Register(std::atomic<int>& value) {
  parms.emplace_back(PARM_TYPE::ATOMIC_INT, &value);
}

void Parameters::Register(float& value) {
  parms.emplace_back(PARM_TYPE::FLOAT, &value);
}

void Parameters::Register(std::atomic<float>& value) {
  parms.emplace_back(PARM_TYPE::ATOMIC_FLOAT, &value);
}

void Parameters::Register(std::string& value) {
  parms.emplace_back(PARM_TYPE::STRING, &value);
}

void Parameters::Register(std::vector<std::string>& list) {
  parms.emplace_back(PARM_TYPE::LIST, &list);
}

void Parameters::RegisterClear(parmFunc f) {
  onClear = f;
}

void Parameters::RegisterParse(parmFunc f) {
  onParse = f;
}

void Parameters::SetDoc(const std::string& name, const std::string& defaultValue,
                        const std::string& doc, const std::string& range) {
  docs.push_back({name, defaultValue, doc, range});
}

const std::string& Parameters::Get() {
  return current;
}

bool Parameters::NeedsRebuild() const {
  if (onClear != nullptr || onParse != nullptr) return true;
  for (const parameter& p : parms) {
    if (p.type == STRING || p.type == LIST) return true;
  }
  return false;
}

int Parameters::BuildPlan(const std::string& args, ParamOp* ops, int cap) {
  if (args.size() == 0) return 0;

  int count = 0;
  size_t pos = 0;
  unsigned int currentArg = 0;
  std::string arg = args + " "; // important to get the last argument
  std::string token;
  while ((pos = arg.find(' ')) != std::string::npos) {
    token = arg.substr(0, pos);

    if (currentArg < parms.size()) {
      if (count >= cap) return -1;
      switch (parms[currentArg].type) {
      case FLOAT:
      case ATOMIC_FLOAT: {
        ops[count].type = parms[currentArg].type;
        ops[count].dest = parms[currentArg].value;
        ops[count].f = std::stof(token);
        count++;
        break;
      }
      case INT:
      case ATOMIC_INT: {
        ops[count].type = parms[currentArg].type;
        ops[count].dest = parms[currentArg].value;
        ops[count].i = std::stoi(token);
        count++;
        break;
      }
      default:
        // STRING/LIST (or unknown) cannot be patched in place — signal the
        // caller to take the structural-rebuild path instead.
        return -1;
      }
    } else {
      INTERNAL::LogImpl().emit(E_DEBUG, "Too many arguments for this object.");
    }
    arg.erase(0, pos + 1);
    currentArg++;
  }

  current = args;
  return count;
}

void Parameters::Set(const std::string& args) {
  if (onClear != nullptr) onClear();
  if (args.size() == 0) return;

  current = args;
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
        break;
      }
      case LIST: {
        ((std::vector<std::string>*)parms[currentArg].value)->clear();
        ((std::vector<std::string>*)parms[currentArg].value)->push_back(token);
        break;
      }
      default: {
        INTERNAL::LogImpl().emit(E_DEBUG, "Argument type unknown.");
        break;
      }
      }

    } else {
      // if last param is a list, add everything to this list
      if (parms.back().type == LIST) {
        ((std::vector<std::string>*)parms.back().value)->push_back(token);
      }
      INTERNAL::LogImpl().emit(E_DEBUG, "Too many arguments for this object.");
    }
    arg.erase(0, pos + 1);
    currentArg++;
  }

  if (onParse != nullptr) {
    onParse();
  }
}
