#pragma once

#include <memory>

#include "basePage.h"
#include "../YseEngine/yse.hpp"
#include "../YseEngine/synth/sineVoice.hpp"
#include "../YseEngine/synth/positionHandlers.hpp"

// Demo20 — Swarm (issue #180, epic #148 spatial showcase).
//
// A polyphonic synth whose per-note position handler is the orbit/swarm handler
// (#170): every held note orbits a shared, steerable centre, so a chord is
// heard circling the listener. Aftertouch widens the swarm radius. The console
// keys steer the swarm centre and apply channel-wide aftertouch so the effect is
// audible without a controller. This scene needs no bundled asset — it always
// runs.
class DemoSwarm : public basePage {
public:
  DemoSwarm();
  ~DemoSwarm();

  void ExplainDemo() override;
  void ShowStatus() override;

private:
  void PlaySwarm();
  void StopSwarm();
  void CenterLeft();
  void CenterRight();
  void MoreAftertouch();
  void LessAftertouch();

  bool playing_ = false;
  float centerX_ = 0.f;
  float aftertouch_ = 0.f;

  std::unique_ptr<YSE::SYNTH::sineVoice> voiceProto_;
  std::unique_ptr<YSE::SYNTH::orbitHandler> handlerProto_;
  std::unique_ptr<YSE::synth> synth_;
  std::unique_ptr<YSE::sound> sound_;
};
