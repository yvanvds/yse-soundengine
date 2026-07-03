
#include "outlet.h"
#include "inlet.h"
#include "pObject.h"
#include "graphState.h"
#include "../dsp/buffer.hpp"

using namespace YSE::PATCHER;

outlet::outlet(pObject* owner, YSE::OUT_TYPE type) : type(type), owner(owner) {}

outlet::~outlet() {
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->Disconnect(this);
  }
}

// Resolve this outlet's fan-out. When the owning patcher is mid-block it hands
// back a pinned, immutable GraphState and we read the snapshot's adjacency (the
// audio-thread path — never touches the live ``connections`` vector). Outside a
// block, or for a standalone object with no patcher, ``graph`` is null and we
// fall back to the live wiring (control-thread / unit-test path). See #226.
const std::vector<YSE::PATCHER::inlet*>& outlet::resolveTargets() const {
  const GraphState* graph = owner ? owner->CurrentBlockGraph() : nullptr;
  if (graph != nullptr && graphId >= 0 &&
      static_cast<size_t>(graphId) < graph->outletTargets.size()) {
    return graph->outletTargets[graphId];
  }
  return connections;
}

void outlet::SendBang(YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetBang(thread);
  }
}

void outlet::SendFloat(float value, YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetFloat(value, thread);
  }
}

void outlet::SendInt(int value, YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetInt(value, thread);
  }
}

void outlet::SendList(const std::string& value, YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetList(value, thread);
  }
}

void outlet::SendMessage(const std::string& value, YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetMessage(value, thread);
  }
}

void outlet::SendBuffer(YSE::DSP::buffer* value, YSE::THREAD thread) {
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetBuffer(value, thread);
  }
}

void outlet::UnwireFromPeers() {
  // Clear first so any reentrant inlet->Disconnect(this) (the buffer/dsp
  // branch calls back into this outlet) finds nothing to erase mid-iteration.
  std::vector<inlet*> conns;
  conns.swap(connections);
  for (unsigned int i = 0; i < conns.size(); i++) {
    conns[i]->Disconnect(this);
  }
}

void outlet::Connect(inlet* in) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == in) return;
  }
  connections.push_back(in);
}

void outlet::Disconnect(inlet* in) {
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == in) {
      connections.erase(connections.begin() + i);
    }
  }
}

void outlet::DumpJSON(nlohmann::json::value_type& json) {
  json["Count"] = connections.size();
  for (unsigned int i = 0; i < connections.size(); i++) {
    json[std::to_string(i)]["Object"] = connections[i]->GetObjectID();
    json[std::to_string(i)]["Inlet"] = connections[i]->GetPosition();
  }
}

unsigned int outlet::GetConnections() {
  return static_cast<unsigned int>(connections.size());
}

unsigned int outlet::GetTarget(unsigned int connection) {
  if (connection < connections.size()) {
    return connections[connection]->GetObjectID();
  }
  return 0;
}

unsigned int outlet::GetTargetInlet(unsigned int connection) {
  if (connection < connections.size()) {
    return connections[connection]->GetPosition();
  }
  return 0;
}

void outlet::SetDoc(const std::string& label, const std::string& doc, const std::string& range) {
  docLabel = label;
  docDescription = doc;
  docRange = range;
}
