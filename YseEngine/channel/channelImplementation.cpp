/*
  ==============================================================================

  channelImplementation.cpp
  Created: 30 Jan 2014 4:21:26pm
  Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CHANNEL::implementationObject::implementationObject(channel* head)
  : head(head),
    newVolume(1.f),
    lastVolume(1.f),
    parent(nullptr),
    insert_dsp(nullptr),
    allowVirtual(true) {}

YSE::CHANNEL::implementationObject::~implementationObject() noexcept {
  try {
    // exit the dsp thread for this channel
    join();

    // The primary disconnect path is on the audio thread, in
    // CHANNEL::Manager::update at the OBJECT_RELEASE→OBJECT_DELETE transition.
    // This guard ensures the slow-pool's destructor only touches
    // parent->children when no audio-thread disconnect has happened (i.e.
    // setup-failure path: channels that died before connect() ran).
    if (INTERNAL::Global().isActive()) {
      if (parent != nullptr &&
          connectedToParent.load(
              std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with
                                            // release in doThisWhenReady() /
                                            // Manager::update() for lock-free handshake
        parent->disconnect(this);
        childrenToParent();
      }
    }

    // Sever the insert plugin's back-reference before we die, so a plugin that
    // outlives this channel (e.g. a process-lifetime static destroyed at exit)
    // can't write through calledfrom into our freed insert_dsp field. Mirrors
    // the sound-path guard in ~SOUND::implementationObject (#298).
    if (insert_dsp != nullptr && insert_dsp->calledfrom != nullptr) {
      insert_dsp->calledfrom = nullptr;
    }

    if (head.load() != nullptr) {
      head.load()->pimpl = nullptr;
    }
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             "CHANNEL::implementationObject destructor swallowed exception");
  }
}

Bool YSE::CHANNEL::implementationObject::connect(CHANNEL::implementationObject* ch) {
  if (ch != this) {
    if (ch->parent != nullptr) ch->parent->disconnect(ch);
    ch->parent = this;
    children.push_front(ch);
    return true;
  } else
    ch->parent = nullptr;
  return false;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::CHANNEL::implementationObject* ch) {
  children.remove(ch);
  return true;
}

Bool YSE::CHANNEL::implementationObject::connect(YSE::SOUND::implementationObject* s) {
  if (s->parent != nullptr && s->parent != this) {
    s->parent->disconnect(s);
  }
  s->parent = this;
  sounds.push_front(s);
  return true;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::SOUND::implementationObject* s) {
  sounds.remove(s);
  return true;
}

void YSE::CHANNEL::implementationObject::run() {
  // A return bus is dispatched to the fast pool from processReturns() to run its
  // insert chain over the already-accumulated `out`; an ordinary channel runs
  // the normal source-tree dsp(). Both write only this impl's own `out`
  // (single-writer discipline, issue #165).
  if (isReturn)
    processReturnInsert();
  else
    dsp();
}

void YSE::CHANNEL::implementationObject::dsp() {
  // if no sounds or other channels are linked, we skip this channel
  if (children.empty() && sounds.empty()) return;

  // clear channel buffer
  clearBuffers();

  // calculate child channels if there are any
  for (auto i = children.begin(); i != children.end(); ++i) {
    // Skip the fast-pool dispatch for children that have no work this
    // render. Their dsp() would early-return at the top of this same
    // function anyway, but the dispatch + matching join() in
    // buffersToParent() would otherwise spin-sleep in 2 ms increments
    // waiting for the worker to wake up and signal isDone. YSE's
    // System().init() creates five default child channels of master
    // (ambient/fx/music/gui/voice — see system.cpp); apps that don't
    // use them — and especially silent / post-stop graphs where the
    // audio thread has no source work to fill the dispatch→join gap —
    // were losing ~10 ms of fake wall-clock per callback waiting for
    // those no-op workers. Observed as the cpuLoad post-stop spike in
    // issue #82.
    if ((*i)->children.empty() && (*i)->sounds.empty()) continue;
    INTERNAL::Global().addFastJob(*i);
  }

  // calculate sounds in this channel
  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    if ((*i)->dsp()) {
      (*i)->toChannels();
    }
  }

  // Pre-fader insert chain: process the summed channel signal in place, before
  // reverb and before the channel volume is applied (buffersToParent). The
  // `out` vector is MULTICHANNELBUFFER-shaped, so it type-matches
  // dspObject::process (N-channel contract, #158).
  if (insert_dsp != nullptr) processInsertDSP();

  REVERB::Manager().process(this);

  if (INTERNAL::UnderWaterEffect().channel() == this) {
    INTERNAL::UnderWaterEffect().apply(out);
  }

  // Publish pre-volume peak for VU consumers. Sized in lock-step with `out`
  // in setup()/resize(); the std::min guards against a resize race.
  const UInt n = (UInt)std::min(out.size(), lastPeakLinearPre.size());
  for (UInt i = 0; i < n; ++i) {
    lastPeakLinearPre[i].v.store(out[i].maxValue(), std::memory_order_release);
  }
}

void YSE::CHANNEL::implementationObject::removeInterface() {
  head.store(nullptr);
}

void YSE::CHANNEL::implementationObject::buffersToParent() {
  join();

  // The master channel drives the send/return phases (issue #165). It is the
  // only channel with no parent, and its body runs after the whole source tree
  // has been recursed — the point where every source send has landed in the
  // return buffers.
  const bool isMaster = (parent == nullptr);

  // (0) Zero every return buffer before any source send taps into it. Runs after
  //     the parallel dsp() phase (which never touches returns), so it races
  //     nothing, and before the recursion below where sends accumulate.
  if (isMaster) CHANNEL::Manager().zeroReturnBuffers();

  // (1) call this recursively on all child channels. Each source channel taps
  //     its sends into the return buffers during its own buffersToParent().
  for (auto i = children.begin(); i != children.end(); ++i) {
    (*i)->buffersToParent();
  }

  // (2) generation-ordered returns phase: each return's insert chain runs on the
  //     fast pool, then folds into master — after all source sends, before the
  //     master fader.
  if (isMaster) CHANNEL::Manager().processReturns(this);

  // A channel with no sounds or subchannels never filled `out` this block, so
  // its buffer is stale; it must neither tap sends nor sum into its parent.
  const bool hasWork = !(children.empty() && sounds.empty());

  // Pre-fader send taps read `out` before adjustVolume() scales it.
  if (hasWork && !sends.empty()) runSendTaps(true);

  // apply channel volume
  adjustVolume();

  // Publish post-volume peak for VU consumers — measured after adjustVolume()
  // so the reading reflects what listeners actually hear from this channel.
  // The master channel also reaches this point, giving a true master VU.
  {
    const UInt n = (UInt)std::min(out.size(), lastPeakLinearPost.size());
    for (UInt i = 0; i < n; ++i) {
      lastPeakLinearPost[i].v.store(out[i].maxValue(), std::memory_order_release);
    }
  }

  // Post-fader send taps read the faded `out` (the default aux-send behaviour).
  if (hasWork && !sends.empty()) runSendTaps(false);

  // if this is the main channel, we're done here
  if (isMaster) return;
  if (!hasWork) return;

  // if not the main channel, add output to parent channel
  for (UInt i = 0; i < out.size(); ++i) {
    // parent size is not checked but should be ok because it's adjusted before calling this
    parent->out[i] += out[i];
  }
}

void YSE::CHANNEL::implementationObject::attachUnderWaterFX() {
  INTERNAL::UnderWaterEffect().channel(this);
}

void YSE::CHANNEL::implementationObject::addDSP(DSP::dspObject* ptr) {
  // Detach any previously-attached plugin first and clear its back-reference
  // (calledfrom) — otherwise, when that old plugin is destructed later, its
  // destructor's `*calledfrom = nullptr` would write to this impl's insert_dsp
  // field, which may already have been freed. Same discipline as
  // SOUND::implementationObject::addDSP (the sound-path UAF, #298). Pointer
  // swap only: safe on the audio thread, no allocation or locking. A null
  // `ptr` simply detaches the chain.
  if (insert_dsp != nullptr) {
    insert_dsp->calledfrom = nullptr;
  }

  insert_dsp = ptr;
  if (insert_dsp != nullptr) {
    insert_dsp->calledfrom = &insert_dsp;
  }
}

void YSE::CHANNEL::implementationObject::processInsertDSP() {
  DSP::dspObject* ptr = insert_dsp;
  while (ptr != nullptr) {
    if (!ptr->bypass()) ptr->process(out);
    ptr = ptr->link();
  }
}

void YSE::CHANNEL::implementationObject::accumulateSend(sendSlot& s) {
  // src is this channel's finalized `out`; dst is the target return's `out`.
  // Runs on the audio thread only, after this channel's dsp() job is joined, so
  // `out` is complete and the return buffer receives from many senders strictly
  // in sequence on one thread — no lock, no atomic (design §8). The ramp is
  // fused into the multiply-accumulate exactly like adjustVolume().
  implementationObject* tgt = s.target;
  // A send may be wired (ADD_SEND applied on the audio thread) before its target
  // return has finished setup() on the slow pool: the source impl is already
  // live, so its next block would read/write the return's `out` while the slow
  // pool is still resizing it — a data race (issue #165). Gate on the target
  // reaching OBJECT_READY, the same acquire/release handshake readyCheck uses to
  // publish the resized buffers; until then the return is not rendering anyway,
  // so skipping loses nothing. A single atomic load, no lock or allocation.
  if (tgt->objectStatus.load(std::memory_order_acquire) != OBJECT_READY) return;
  const UInt n = (UInt)std::min(out.size(), tgt->out.size());
  if (s.lastLevel == s.newLevel) {
    const Flt level = s.newLevel;
    if (level == 0.f) return; // soft-muted send: nothing to add
    for (UInt i = 0; i < n; ++i) {
      Flt* dst = tgt->out[i].getPtr();
      const Flt* src = out[i].getPtr();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; ++j)
        dst[j] += src[j] * level;
    }
  } else {
    const Flt step = (s.newLevel - s.lastLevel) / STANDARD_BUFFERSIZE;
    for (UInt i = 0; i < n; ++i) {
      Flt* dst = tgt->out[i].getPtr();
      const Flt* src = out[i].getPtr();
      Flt level = s.lastLevel;
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; ++j) {
        dst[j] += src[j] * level;
        level += step;
      }
    }
    s.lastLevel = s.newLevel;
  }
}

void YSE::CHANNEL::implementationObject::runSendTaps(bool preFaderPhase) {
  for (auto& s : sends) {
    if (s.target != nullptr && s.preFader == preFaderPhase) accumulateSend(s);
  }
}

void YSE::CHANNEL::implementationObject::processReturnInsert() {
  // `out` already holds the accumulated sends (zeroed at block start by the
  // master, summed by the source tree and any earlier-generation returns). Run
  // the return's own DSP in place — its insert chain is the effect (e.g. a plate
  // reverb on a send return), plus any attached global reverb / underwater FX —
  // mirroring the tail of dsp(). Single-writer over this return's own `out`.
  if (insert_dsp != nullptr) processInsertDSP();

  REVERB::Manager().process(this);

  if (INTERNAL::UnderWaterEffect().channel() == this) {
    INTERNAL::UnderWaterEffect().apply(out);
  }

  // Publish pre-volume peak (mirrors dsp()).
  const UInt n = (UInt)std::min(out.size(), lastPeakLinearPre.size());
  for (UInt i = 0; i < n; ++i) {
    lastPeakLinearPre[i].v.store(out[i].maxValue(), std::memory_order_release);
  }
}

void YSE::CHANNEL::implementationObject::finalizeReturn(implementationObject* master) {
  // Serial audio-thread finalize, run after this return's processReturnInsert()
  // job has been joined (design §7). Pre-fader taps read the return's `out`
  // before its own fader; post-fader taps read it after. A return's send targets
  // are always a strictly-higher generation, whose buffers have not yet been
  // dispatched, so this accumulation is safe.
  if (!sends.empty()) runSendTaps(true);

  adjustVolume(); // the return's own fader

  {
    const UInt n = (UInt)std::min(out.size(), lastPeakLinearPost.size());
    for (UInt i = 0; i < n; ++i) {
      lastPeakLinearPost[i].v.store(out[i].maxValue(), std::memory_order_release);
    }
  }

  if (!sends.empty()) runSendTaps(false);

  // Fold the return into the master mix. A return always contributes (its insert
  // chain may still be ringing out a reverb tail after its senders fell silent),
  // so there is no hasWork guard here.
  const UInt n = (UInt)std::min(out.size(), master->out.size());
  for (UInt i = 0; i < n; ++i) {
    master->out[i] += out[i];
  }
}

void YSE::CHANNEL::implementationObject::detachSends() {
  // Audio-thread teardown at OBJECT_RELEASE, before the impl can be freed by the
  // slow pool (design §9). Two directions:
  //
  // 1. This channel's own outgoing sends: unlink each active slot from its
  //    target return's registry so the target never dangles.
  for (auto& s : sends) {
    if (s.target != nullptr) {
      s.target->sendRegistry.remove(&s);
      s.target = nullptr;
    }
  }

  // 2. If this is a return, sever every slot that still points at us (the
  //    many-to-one case) and unlink from the manager's returns list, so no live
  //    send slot can write into our freed `out` next block.
  if (isReturn) {
    for (auto i = sendRegistry.begin(); i != sendRegistry.end();) {
      sendSlot* s = *i;
      ++i; // advance (reads s->regNext) BEFORE mutating the slot
      s->target = nullptr; // disable the sender's slot
    }
    sendRegistry.clear(); // detach every regNext link
    CHANNEL::Manager().unlinkReturn(this);
  }
}

void YSE::CHANNEL::implementationObject::setup() {
  if (objectStatus >= OBJECT_CREATED) {
    if (objectStatus == OBJECT_READY) return;

    const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
    out.resize(numOutputs);
    outConf.resize(numOutputs);
    lastPeakLinearPre.resize(numOutputs);
    lastPeakLinearPost.resize(numOutputs);
    for (UInt i = 0; i < numOutputs; i++) {
      outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
      outConf[i].isLFE = CHANNEL::Manager().getOutputIsLFE(i);
    }
    computeEffectiveSpeakerWeights(outConf);

    // Size the send-slot vector exactly once, here on the slow pool, before the
    // channel ever reaches the render path (issue #165). Never resized
    // afterwards, so `&sends[i]` stays a stable back-reference node. Guarded so a
    // repeated setup() (e.g. readyCheck bouncing the status back to CREATED)
    // cannot reallocate it.
    if (!sendsSized) {
      sends.resize(sendSlotCount > 0 ? (std::size_t)sendSlotCount : 0);
      sendsSized = true;
    }

    objectStatus = OBJECT_SETUP;
  }
}

void YSE::CHANNEL::implementationObject::resize(bool deep) {
  const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
  out.resize(numOutputs);
  outConf.resize(numOutputs);
  lastPeakLinearPre.resize(numOutputs);
  lastPeakLinearPost.resize(numOutputs);
  for (UInt i = 0; i < numOutputs; i++) {
    outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
    outConf[i].isLFE = CHANNEL::Manager().getOutputIsLFE(i);
  }
  computeEffectiveSpeakerWeights(outConf);
  if (deep) {
    for (auto i = children.begin(); i != children.end(); ++i) {
      (*i)->resize(true);
    }

    for (auto i = sounds.begin(); i != sounds.end(); ++i) {
      (*i)->resize();
    }
  }
}

void YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(
    std::vector<output>& outConf) {
  // The density-compensation term effective[i] = Σ_j overlap(angle_i, angle_j)
  // depends only on the speaker geometry, not on the source or the audio block.
  // Precompute it here (layout-change time) so toChannels() can read it instead
  // of paying the O(N^2) cos cost per source channel per block (issue #211).
  // LFE outputs are excluded from the azimuth pan, matching toChannels() which
  // skips them in both the i and j loops (issue #203); their effective is left
  // at the default and never read. The summation order over non-LFE speakers is
  // kept identical to the old inline loop so the result is bit-for-bit the same.
  for (UInt i = 0; i < outConf.size(); i++) {
    if (outConf[i].isLFE) continue;
    Flt sum = 0;
    for (UInt j = 0; j < outConf.size(); j++) {
      if (outConf[j].isLFE) continue;
      sum += SOUND::implementationObject::computeSpeakerOverlap(outConf[i].angle, outConf[j].angle);
    }
    outConf[i].effective = sum;
  }
}

Bool YSE::CHANNEL::implementationObject::readyCheck() {
  // this means we have don this check before and returned true back then.
  // the object is added to the list of inUse, but is probably not deleted just
  // yet. It will be deleted the next time the remove_if function runs (in objectManager)
  if (objectStatus == OBJECT_READY) {
    return false;
  }
  if (objectStatus == OBJECT_SETUP) {
    if (outConf.size() == CHANNEL::Manager().getNumberOfOutputs()) {
      objectStatus = OBJECT_READY;
      return true;
    }
  }
  objectStatus = OBJECT_CREATED;
  return false;
}

void YSE::CHANNEL::implementationObject::doThisWhenReady() {
  // A return bus is excluded from the source dsp tree: it is NOT linked into any
  // parent's `children`, so dsp() never dispatches it and buffersToParent()'s
  // recursion never visits it. Instead it joins the manager's audio-thread
  // `returns` list and is processed in the explicit returns phase (issue #165).
  // connectedToParent stays false, so the release/destructor parent-disconnect
  // path is skipped for returns (they tear down via detachSends()).
  if (isReturn) {
    CHANNEL::Manager().linkReturn(this);
    return;
  }

  parent->connect(this);
  // Audio thread now owns the link into parent->children. Mark it so the
  // release path can disconnect us before the slow-pool deleteJob frees us.
  connectedToParent.store(
      true, std::memory_order_release); // NOSONAR S8417: intentional release — publishes the
                                        // audio-thread connect to the dtor's acquire load
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::CHANNEL::implementationObject::getStatus() {
  return objectStatus.load();
}

void YSE::CHANNEL::implementationObject::setStatus(YSE::OBJECT_IMPLEMENTATION_STATE value) {
  objectStatus.store(value);
}

void YSE::CHANNEL::implementationObject::sync() {
  if (head.load() == nullptr) {
    objectStatus = OBJECT_RELEASE;
    return;
  }

  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }
}

void YSE::CHANNEL::implementationObject::parseMessage(const messageObject& message) {
  switch (message.ID) {
  case ATTACH_REVERB:
    REVERB::Manager().attachToChannel(this);
    break;
  case ATTACH_DSP:
    addDSP((DSP::dspObject*)message.ptrValue);
    break;
  case MOVE: {
    channel* ptr = (channel*)message.ptrValue;
    if (ptr != nullptr) {
      ptr->pimpl->connect(this);
    }
    break;
  }
  case VIRTUAL:
    allowVirtual = message.boolValue;
    break;
  case VOLUME:
    newVolume = message.floatValue;
    break;
  case ADD_SEND: {
    // (Re)point a send slot at a return and link its back-reference. By the time
    // this arrives, setup() has already sized `sends` (a channel reaches sync()
    // only after readyCheck, which follows setup()), but bounds-check defensively.
    const Int slot = message.send.slot;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sendSlot& s = sends[slot];
      // Detach the slot's previous target from its registry before repointing.
      if (s.target != nullptr) s.target->sendRegistry.remove(&s);
      s.target = (implementationObject*)message.send.target;
      s.preFader = message.send.preFader;
      // Start the ramp from silence so a freshly-wired send fades in click-free;
      // the SEND_LEVEL message that follows sets the target level.
      s.lastLevel = 0.f;
      s.newLevel = 0.f;
      if (s.target != nullptr) s.target->sendRegistry.push_front(&s);
    }
    break;
  }
  case SEND_LEVEL: {
    const Int slot = message.sendLevel.slot;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sends[slot].newLevel = message.sendLevel.level;
    }
    break;
  }
  case REMOVE_SEND: {
    const Int slot = (Int)message.uintValue;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sendSlot& s = sends[slot];
      if (s.target != nullptr) {
        s.target->sendRegistry.remove(&s);
        s.target = nullptr;
      }
      s.lastLevel = 0.f;
      s.newLevel = 0.f;
    }
    break;
  }
  case SET_GENERATION:
    generation = (Int)message.uintValue;
    break;
  }
}

void YSE::CHANNEL::implementationObject::childrenToParent() {
  // don't do this if there is no parent channel
  if (parent == nullptr) return;

  {
    auto i = children.begin();
    while (i != children.end()) {
      parent->connect(*i);
      i = children.begin();
    }
  }

  {
    auto i = sounds.begin();
    while (i != sounds.end()) {
      parent->connect(*i);
      i = sounds.begin();
    }
  }
}

void YSE::CHANNEL::implementationObject::clearBuffers() {
  for (UInt i = 0; i < out.size(); ++i) {
    out[i] = 0.0f;
  }
}

void YSE::CHANNEL::implementationObject::adjustVolume() {
  if (newVolume != lastVolume) {
    // new value, create a ramp
    Flt step = (newVolume - lastVolume) / STANDARD_BUFFERSIZE;

    for (UInt i = 0; i < out.size(); ++i) {
      Flt multiplier = lastVolume;
      Flt* ptr = out[i].getPtr();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; j++) {
        *ptr++ *= multiplier;
        multiplier += step;
      }
    }
    lastVolume = newVolume;
  } else {
    // same volume, just copy
    for (UInt i = 0; i < out.size(); ++i) {
      out[i] *= newVolume;
    }
  }
}

int YSE::CHANNEL::implementationObject::getNumOutputs() const {
  return static_cast<int>(out.size());
}

float YSE::CHANNEL::implementationObject::getPeakLinearPre(int outputIdx) const {
  if (outputIdx < 0 || static_cast<size_t>(outputIdx) >= lastPeakLinearPre.size()) return 0.f;
  return lastPeakLinearPre[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPost(int outputIdx) const {
  if (outputIdx < 0 || static_cast<size_t>(outputIdx) >= lastPeakLinearPost.size()) return 0.f;
  return lastPeakLinearPost[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPreCombined() const {
  float peak = 0.f;
  for (const auto& cell : lastPeakLinearPre) {
    const float v = cell.v.load(std::memory_order_acquire);
    if (v > peak) peak = v;
  }
  return peak;
}

float YSE::CHANNEL::implementationObject::getPeakLinearPostCombined() const {
  float peak = 0.f;
  for (const auto& cell : lastPeakLinearPost) {
    const float v = cell.v.load(std::memory_order_acquire);
    if (v > peak) peak = v;
  }
  return peak;
}
