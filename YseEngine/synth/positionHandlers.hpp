/*
  ==============================================================================

    positionHandlers.hpp
    The three built-in position handlers shipped by issue #170.

    - staticHandler       : a fixed position (or offset). The trivial default.
    - randomSpreadHandler : a random point in a radius around a centre, drawn
                            once per note-on (so voice steals re-randomise). Its
                            RNG is seeded deterministically, so a given seed
                            reproduces the same scatter every run.
    - orbitHandler        : the swarm workhorse — each note orbits a shared,
                            steerable centre with a per-note phase and a radius
                            derived from velocity and aftertouch. Doubles as the
                            reference for writing a custom handler.

    All three keep their configuration in members set (chainably) on the
    prototype before attach; clone() copies that configuration to every slot.
    The centre is read live from the synth's shared handler-param block
    (indices 0..2), so it can be steered at runtime with synth::handlerParam().
    See docs/design/per_note_positioning.md §8 and §15 (the worked swarm).

  ==============================================================================
*/

#ifndef YSE_SYNTH_POSITIONHANDLERS_HPP
#define YSE_SYNTH_POSITIONHANDLERS_HPP

#include <cstdint>

#include "../headers/defines.hpp" // API
#include "../headers/types.hpp"
#include "../utils/vector.hpp" // Pos
#include "positionHandler.hpp"

namespace YSE {
  namespace SYNTH {

    /** Shared handler-param indices the built-in handlers read for their
        steerable centre (written with ``synth::handlerParam(index, value)``). */
    enum HandlerParamIndex {
      HP_CENTER_X = 0,
      HP_CENTER_Y = 1,
      HP_CENTER_Z = 2,
    };

    /**
     *  @brief Places every note at one fixed position. The trivial default.
     *
     *  Ignores the shared centre and all live values — a genuinely static
     *  source. Use ``synth::notePosition()`` for app-driven trajectories when
     *  no movement behaviour is wanted.
     */
    class API staticHandler : public positionHandler {
    public:
      /** @brief Set the fixed position (same frame as a YSE::sound position). */
      staticHandler& position(const Pos& p) {
        position_ = p;
        return *this;
      }
      /** @brief Current fixed position. */
      Pos position() const {
        return position_;
      }

      positionHandler* clone() override;
      Pos noteOn(const voiceContext& ctx) override;
      Pos update(const voiceContext& ctx, Flt delta) override;

    private:
      Pos position_{0.f};
    };

    /**
     *  @brief Scatters each note to a random point within ``radius`` of the
     *         shared centre, drawn once at note-on and held for the note.
     *
     *  The draw uses a small deterministic PRNG seeded from ``seed()`` plus a
     *  per-slot offset, so the same seed reproduces the same scatter every run
     *  (the basis of the seeded-trajectory tests). A voice steal re-draws in
     *  ``noteOn()``, so a stolen slot re-randomises correctly.
     */
    class API randomSpreadHandler : public positionHandler {
    public:
      /** @brief Radius of the spread sphere around the centre. */
      randomSpreadHandler& radius(Flt r) {
        radius_ = r;
        return *this;
      }
      Flt radius() const {
        return radius_;
      }
      /** @brief Base RNG seed. Each cloned slot derives a distinct stream from
       *  it, so the whole synth is reproducible for a given seed. */
      randomSpreadHandler& seed(uint32_t s) {
        baseSeed_ = s;
        return *this;
      }

      positionHandler* clone() override;
      Pos noteOn(const voiceContext& ctx) override;
      Pos update(const voiceContext& ctx, Flt delta) override;

    private:
      // xorshift32: allocation-free, lock-free, deterministic — RT-safe to draw
      // from on the audio thread. Returns [0, 1).
      Flt nextRandom();
      Pos center(const voiceContext& ctx) const;

      Flt radius_ = 1.f;
      uint32_t baseSeed_ = 0x9E3779B9u; // golden-ratio constant; non-zero
      uint32_t cloneCounter_ = 0; // advanced on the prototype, per clone()
      uint32_t rngState_ = 0x9E3779B9u; // per-instance stream state (non-zero)
      Pos offset_{0.f}; // the per-note draw, set in noteOn()
    };

    /**
     *  @brief The swarm handler — each note orbits a shared, steerable centre.
     *
     *  This is the epic's showcase and the template for a user handler. Each
     *  note gets a distinct starting phase (from its note number), a radius
     *  derived from velocity (and widened live by aftertouch), and advances its
     *  phase at ``rate`` rad/s. The centre is read live from handler-params
     *  0..2, so ``synth::handlerParam()`` recentres the whole swarm from the
     *  main thread with one bounded message. On release the orbit slows, so a
     *  released note keeps moving — audibly correct — through its decay tail.
     */
    class API orbitHandler : public positionHandler {
    public:
      /** @brief Base orbit radius (added to the velocity-scaled term). */
      orbitHandler& radius(Flt r) {
        radius_ = r;
        return *this;
      }
      /** @brief Extra radius added at full velocity. */
      orbitHandler& velocityRadius(Flt r) {
        velocityRadius_ = r;
        return *this;
      }
      /** @brief Fraction of extra radius added at full aftertouch (swarm
       *  widening). */
      orbitHandler& aftertouchWiden(Flt frac) {
        aftertouchWiden_ = frac;
        return *this;
      }
      /** @brief Orbit angular speed in radians per second. */
      orbitHandler& rate(Flt radiansPerSecond) {
        rate_ = radiansPerSecond;
        return *this;
      }
      /** @brief Vertical offset of the orbit plane from the centre. */
      orbitHandler& height(Flt h) {
        height_ = h;
        return *this;
      }
      /** @brief Multiplier applied to ``rate`` once the note is released. */
      orbitHandler& releaseSlow(Flt factor) {
        releaseSlow_ = factor;
        return *this;
      }

      positionHandler* clone() override;
      Pos noteOn(const voiceContext& ctx) override;
      Pos update(const voiceContext& ctx, Flt delta) override;
      void onRelease(const voiceContext& ctx) override;

    private:
      Pos positionAt(const voiceContext& ctx, Flt phase) const;

      // config (copied to every clone)
      Flt radius_ = 1.f;
      Flt velocityRadius_ = 2.f;
      Flt aftertouchWiden_ = 1.f;
      Flt rate_ = 2.f;
      Flt height_ = 0.f;
      Flt releaseSlow_ = 0.5f;
      // per-note state (fully reset in noteOn)
      Flt phase_ = 0.f; ///< current orbit angle
      Flt speed_ = 2.f; ///< current angular speed (rate_, halved on release)
      Flt noteRadius_ = 1.f; ///< radius chosen for the current note
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_POSITIONHANDLERS_HPP
