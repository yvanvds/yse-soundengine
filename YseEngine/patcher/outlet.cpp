
#include "outlet.h"
#include "inlet.h"
#include "pObject.h"
#include "graphState.h"
#include "../dsp/buffer.hpp"

using namespace YSE::PATCHER;

namespace {
  // Wait-free recursion guard for the synchronous message send path (issue
  // #236). A feedback cycle in the message graph — e.g. a gSend/gReceive pair
  // whose receive output is wired back into the send's inlet, or a purely
  // local cycle among generic objects — has no natural termination: a single
  // value entering the cycle recurses through outlet::Send* until the stack
  // overflows (STATUS_STACK_OVERFLOW). Every fan-out edge, whether it travels
  // the local wiring, the in-patcher PassData dispatch, or the global bus,
  // passes back through outlet::Send*, so a depth ceiling here breaks any
  // cycle regardless of the route taken.
  //
  // RT-safety: the counter is thread_local, so each thread (control/GUI and
  // each audio render thread) tracks its own send depth independently. Reading
  // and mutating it is a plain TLS load/store — no allocation, no lock, no
  // syscall — so this is safe on the audio-thread fan-out path where PassData
  // dispatches synchronously (T_DSP). When the ceiling is reached the fan-out
  // is dropped, terminating the cycle instead of crashing.
  //
  // The ceiling is chosen well above any realistic acyclic fan-out depth (a
  // message chain that deep is already pathological) yet low enough that the
  // bounded stack usage stays comfortably within the smallest render-thread
  // stack.
  constexpr unsigned int kMaxSendDepth = 64;
  thread_local unsigned int tSendDepth = 0;

  // RAII depth tracker. ``allowed`` is false when constructing this frame would
  // exceed the ceiling, in which case the caller must not fan out.
  struct SendDepthGuard {
    bool allowed;
    SendDepthGuard() : allowed(tSendDepth < kMaxSendDepth) {
      ++tSendDepth;
    }
    ~SendDepthGuard() {
      --tSendDepth;
    }
  };
} // namespace

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
  SendDepthGuard guard;
  if (!guard.allowed) return;
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetBang(thread);
  }
}

void outlet::SendFloat(float value, YSE::THREAD thread) {
  SendDepthGuard guard;
  if (!guard.allowed) return;
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetFloat(value, thread);
  }
}

void outlet::SendInt(int value, YSE::THREAD thread) {
  SendDepthGuard guard;
  if (!guard.allowed) return;
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetInt(value, thread);
  }
}

void outlet::SendList(const std::string& value, YSE::THREAD thread) {
  SendDepthGuard guard;
  if (!guard.allowed) return;
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetList(value, thread);
  }
}

void outlet::SendMessage(const std::string& value, YSE::THREAD thread) {
  SendDepthGuard guard;
  if (!guard.allowed) return;
  const auto& targets = resolveTargets();
  for (unsigned int i = 0; i < targets.size(); i++) {
    targets[i]->SetMessage(value, thread);
  }
}

void outlet::SendBuffer(YSE::DSP::buffer* value, YSE::THREAD thread) {
  SendDepthGuard guard;
  if (!guard.allowed) return;
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
