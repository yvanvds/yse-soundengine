#include "inlet.h"
#include "outlet.h"
#include "pObject.h"
#include "graphState.h"

using namespace YSE::PATCHER;

inlet::inlet(pObject* obj, bool active, int position)
  : obj(obj),
    dspReady(false),
    active(active),
    position(position),
    onInt(nullptr),
    onBang(nullptr),
    onFloat(nullptr),
    onList(nullptr),
    onBuffer(nullptr),
    dspConnection(nullptr) {}

inlet::~inlet() {
  if (dspConnection != nullptr) {
    dspConnection->Disconnect(this);
  }
  for (unsigned int i = 0; i < connections.size(); i++) {
    connections[i]->Disconnect(this);
  }
}

void inlet::RegisterInt(intFunc f) {
  onInt = f;
}

void inlet::RegisterBang(voidFunc f) {
  onBang = f;
}

void inlet::RegisterFloat(floatFunc f) {
  onFloat = f;
}

void inlet::RegisterList(listFunc f) {
  onList = f;
}

void inlet::RegisterBuffer(bufferFunc f) {
  onBuffer = f;
}

void inlet::SetInt(int value, YSE::THREAD thread) {
  if (onInt) {
    onInt(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetBang(YSE::THREAD thread) {
  if (onBang) {
    onBang(position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetFloat(float value, YSE::THREAD thread) {
  if (onFloat) {
    onFloat(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetList(const std::string& value, YSE::THREAD thread) {
  if (onList) {
    onList(value, position, thread);
    if (active) {
      if (obj->IsDSPObject() && thread == T_GUI) return;
      obj->CalculateIfReady(thread);
    }
  }
}

void inlet::SetBuffer(YSE::DSP::buffer* buffer, YSE::THREAD thread) {
  if (onBuffer) {
    onBuffer(buffer, position, thread);
    dspReady = true;
    if (obj->IsDSPObject() && thread == T_GUI) return;
    obj->CalculateIfReady(thread);
  }
}

void inlet::SetMessage(const std::string& message, YSE::THREAD thread, float value) {
  obj->SetMessage(message, value);
  if (active) {
    if (obj->IsDSPObject() && thread == T_GUI) return;
    obj->CalculateIfReady(thread);
  }
}

bool inlet::WaitingForDSP() const {
  // Whether this inlet has an active buffer input comes from the pinned
  // snapshot when the patcher is mid-block (audio-thread path), else from the
  // live ``dspConnection`` (control-thread / standalone path). See #226.
  const GraphState* graph = obj ? obj->CurrentBlockGraph() : nullptr;
  bool hasDsp;
  if (graph != nullptr && graphId >= 0 &&
      static_cast<size_t>(graphId) < graph->inletHasDsp.size()) {
    hasDsp = graph->inletHasDsp[graphId] != 0;
  } else {
    hasDsp = (dspConnection != nullptr);
  }
  if (!hasDsp) return false;
  return !dspReady;
}

void inlet::UnwireFromPeers() {
  // Detach from the buffer input and every control-edge source. Clear our own
  // lists first so the peers' Disconnect(this) calls stay consistent.
  outlet* dsp = dspConnection;
  dspConnection = nullptr;
  std::vector<outlet*> conns;
  conns.swap(connections);
  if (dsp != nullptr) dsp->Disconnect(this);
  for (unsigned int i = 0; i < conns.size(); i++) {
    conns[i]->Disconnect(this);
  }
}

bool inlet::Connect(outlet* out) {
  if (out->Type() == OUT_TYPE::BUFFER) {
    if (dspConnection == nullptr) {
      dspConnection = out;
      return true;
    } else
      return false;
  } else {
    for (unsigned int i = 0; i < connections.size(); i++) {
      if (connections[i] == out) return false;
    }
    connections.push_back(out);
    return true;
  }
}

void inlet::Disconnect(outlet* out) {
  if (dspConnection == out) {
    dspConnection->Disconnect(this);
    dspConnection = nullptr;
    return;
  }
  for (unsigned int i = 0; i < connections.size(); i++) {
    if (connections[i] == out) {
      connections[i]->Disconnect(this);
      connections.erase(connections.begin() + i);
    }
  }
}

bool inlet::AcceptsDSP() const {
  if (onBuffer) return true;
  return false;
}

bool inlet::HasActiveDSPConnection() const {
  return dspConnection != nullptr;
}

int inlet::GetObjectID() {
  return obj->GetID();
}

int inlet::GetPosition() {
  return position;
}

void inlet::SetDoc(const std::string& label, const std::string& doc, const std::string& range) {
  docLabel = label;
  docDescription = doc;
  docRange = range;
}

unsigned int inlet::GetAcceptedTypes() const {
  unsigned int mask = IT_NONE;
  if (onBuffer) mask |= IT_BUFFER;
  if (onFloat) mask |= IT_FLOAT;
  if (onInt) mask |= IT_INT;
  if (onBang) mask |= IT_BANG;
  if (onList) mask |= IT_LIST;
  return mask;
}