/*
  ==============================================================================

    positionHandlers.cpp
    Definitions for the three built-in position handlers (issue #170). Kept out
    of line so each concrete handler's vtable is anchored in one translation
    unit and exported under the API macro, exactly like SYNTH::sineVoice.

    All hook bodies are allocation-free, lock-free and non-blocking: they run on
    the audio thread. The only non-trivial cost is orbitHandler's two trig calls
    and randomSpreadHandler's xorshift draw — both a handful of FLOPs.

  ==============================================================================
*/

#include "positionHandlers.hpp"

#include <cmath>

namespace YSE {
  namespace SYNTH {

    // ---- staticHandler ----------------------------------------------------

    positionHandler* staticHandler::clone() {
      return new staticHandler(*this);
    }

    Pos staticHandler::noteOn(const voiceContext& /*ctx*/) {
      return position_;
    }

    Pos staticHandler::update(const voiceContext& /*ctx*/, Flt /*delta*/) {
      return position_;
    }

    // ---- randomSpreadHandler ----------------------------------------------

    Flt randomSpreadHandler::nextRandom() {
      // xorshift32 — a fixed-size, allocation-free PRNG safe to draw from on the
      // audio thread. State is seeded non-zero in clone().
      uint32_t x = rngState_;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      rngState_ = x;
      return static_cast<Flt>(x & 0xFFFFFFu) / static_cast<Flt>(0x1000000u);
    }

    Pos randomSpreadHandler::center(const voiceContext& ctx) const {
      return Pos(ctx.handlerParam(HP_CENTER_X), ctx.handlerParam(HP_CENTER_Y),
                 ctx.handlerParam(HP_CENTER_Z));
    }

    positionHandler* randomSpreadHandler::clone() {
      auto* h = new randomSpreadHandler(*this);
      // Derive a distinct, deterministic stream for this slot. cloneCounter_
      // lives on the prototype and advances per clone(), so slots created in
      // order on the setup pool get reproducible, non-overlapping seeds.
      uint32_t s = baseSeed_ + 0x9E3779B9u * (++cloneCounter_);
      if (s == 0) s = 0x1u; // xorshift must never be seeded with 0
      h->rngState_ = s;
      h->cloneCounter_ = 0;
      return h;
    }

    Pos randomSpreadHandler::noteOn(const voiceContext& ctx) {
      // Draw a fresh offset for THIS note (full reinit — the slot may have just
      // finished another note, §11). Three draws mapped to [-radius, +radius].
      offset_.x = (nextRandom() * 2.f - 1.f) * radius_;
      offset_.y = (nextRandom() * 2.f - 1.f) * radius_;
      offset_.z = (nextRandom() * 2.f - 1.f) * radius_;
      return center(ctx) + offset_;
    }

    Pos randomSpreadHandler::update(const voiceContext& ctx, Flt /*delta*/) {
      // Static per note: hold the note-on draw, but track the live centre so a
      // steered centre carries the whole scatter with it.
      return center(ctx) + offset_;
    }

    // ---- orbitHandler -----------------------------------------------------

    // Golden angle (rad): spacing successive note phases by it scatters notes
    // evenly around the ring, so distinct notes trace visibly distinct orbits.
    static const Flt kGoldenAngle = 2.399963229f;

    Pos orbitHandler::positionAt(const voiceContext& ctx, Flt phase) const {
      Pos c(ctx.handlerParam(HP_CENTER_X), ctx.handlerParam(HP_CENTER_Y),
            ctx.handlerParam(HP_CENTER_Z));
      // Radius chosen for this note (from velocity, set in noteOn) widened live
      // by aftertouch, so leaning on a key spreads the swarm outward — the synth
      // forwarding a live value to the handler.
      Flt r = noteRadius_ * (1.f + aftertouchWiden_ * ctx.aftertouch);
      return Pos(c.x + r * std::cos(phase), c.y + height_, c.z + r * std::sin(phase));
    }

    positionHandler* orbitHandler::clone() {
      return new orbitHandler(*this);
    }

    Pos orbitHandler::noteOn(const voiceContext& ctx) {
      // FULL reinit — the instance may have just finished another note (§11).
      phase_ = static_cast<Flt>(ctx.note) * kGoldenAngle; // distinct per note
      speed_ = rate_;
      noteRadius_ = radius_ + velocityRadius_ * ctx.velocity; // velocity -> radius
      return positionAt(ctx, phase_);
    }

    Pos orbitHandler::update(const voiceContext& ctx, Flt delta) {
      phase_ += speed_ * delta; // frame-rate independent advance
      return positionAt(ctx, phase_);
    }

    void orbitHandler::onRelease(const voiceContext& /*ctx*/) {
      speed_ *= releaseSlow_; // slow the orbit through the decay tail
    }

  } // namespace SYNTH
} // namespace YSE
