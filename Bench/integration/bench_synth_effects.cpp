// Tier 3 macro scenarios — audio-thread cost of the synth & effects surface
// (issue #181 bench sweep, closing epic #149). Every case drives the real
// audio-callback body via YSE::System().renderOffline(blocks) — the same path
// PortAudio drives in production — so the numbers are the per-block cost the
// audio thread actually pays.
//
// Covered here:
//   - voice-count scaling      BM_Engine_SynthVoiceScaling/<N>   (N sounding voices)
//   - channel insert-chain cost BM_Engine_ChannelInsertChain*    (EQ→comp→chorus insert)
//   - send fan-in cost          BM_Engine_SendFanIn/<N>          (N channels → one return)
//   - N positioned notes        BM_Engine_PositionedNotes/<N>    (orbit-handler voices)
//
// The per-voice DSP cost of individual voice types (sine / VA / FM / sampler)
// lives in the Tier 1 benches (dsp/bench_va_voice.cpp, bench_fm_voice.cpp,
// bench_sampler_voice.cpp); this file measures how those costs compose through
// the engine's mix tree, polyphony allocator, insert chain and send graph.
//
// Isolation caveat: yse_benchmarks is one process with one process-global
// engine, so a scene left alive by an earlier benchmark (e.g. the static
// sound pool in bench_mixing.cpp) renders alongside these. Each scenario below
// tears its own scene down and drains before returning, so it does not pollute
// later benchmarks; and because registration order is stable per build, any
// constant offset from a sibling benchmark is the same across commits, so the
// scaling deltas these report stay a faithful regression signal.

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "dsp/modules/chorus.hpp"
#include "dsp/modules/compressor.hpp"
#include "dsp/modules/parametricEQ.hpp"
#include "dsp/modules/plateReverb.hpp"
#include "sound/soundInterface.hpp"
#include "synth/positionHandlers.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"

#include "support/bench_helpers.hpp"

#include <benchmark/benchmark.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

  constexpr int kBlocksPerIter = 64;
  constexpr int kMidiChannel = 1;

  // Pump the control plane so create / addVoices / noteOn messages reach the
  // audio side, then render one block — the offline analogue of one engine tick.
  void pump(int blocks) {
    for (int b = 0; b < blocks; ++b) {
      YSE::System().update();
      YSE::System().renderOffline(1);
    }
  }

  // Bring an attached synth up to its full voice count (cloning is async on the
  // setup pool, which runs on its own thread). Yield to that thread between
  // checks — a tight spin starves it and the count never advances. Wall-clock
  // bounded so a failed content/setup path can never hang the bench.
  bool bringReady(YSE::synth& syn, int expectedVoices) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      if (syn.getNumVoices() == expectedVoices) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return syn.getNumVoices() == expectedVoices;
  }

  // A spread of note numbers around middle C so `count` distinct voices sound.
  std::vector<int> chordNotes(int count) {
    std::vector<int> notes;
    notes.reserve(count);
    for (int i = 0; i < count; ++i)
      notes.push_back(48 + (i % 36)); // C3..B5, wrapping for large counts
    return notes;
  }

  // ── voice-count scaling ──────────────────────────────────────────────────────
  //
  // One synth of N sine voices behind one sound, with N notes held at sustain.
  // Measures how the audio-thread render cost grows with polyphony through the
  // real synth aggregate + sound-render + master-mix path.

  void BM_Engine_SynthVoiceScaling(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
      state.SkipWithError("YSE::System().initOffline() failed");
      return;
    }
    const int voices = static_cast<int>(state.range(0));

    YSE::SYNTH::sineVoice proto;
    proto.attack(0.005f).decay(0.05f).sustain(0.8f).release(0.2f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, voices);
      YSE::sound snd;
      snd.create(syn);
      if (!bringReady(syn, voices)) {
        state.SkipWithError("synth did not reach expected voice count");
        return;
      }
      snd.play();
      for (int n : chordNotes(voices))
        syn.noteOn(kMidiChannel, n, 0.8f);
      pump(16); // let every note reach sustain before timing

      for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
      }
      state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);

      syn.allNotesOff();
      snd.stop();
      pump(8); // stop + release before the objects destruct (sound before synth)
    }
    pump(32); // drain deletes so the next benchmark starts from a clean tree
  }
  BENCHMARK(BM_Engine_SynthVoiceScaling)->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(32)->Arg(64);

  // ── channel insert-chain cost ────────────────────────────────────────────────
  //
  // The cost the audio thread pays running a channel's pre-fader insert chain on
  // every device output channel. A single looping source drives the channel; the
  // baseline case runs the same scene with no insert so the chain cost is the
  // delta between the two. The insert modules process the full device layout, so
  // the reported cost already reflects "N device channels" for the open layout.

  YSE::DSP::buffer& insertSource() {
    static YSE::DSP::buffer buf(4096);
    static bool init = false;
    if (!init) {
      buf = 0.25f;
      init = true;
    }
    return buf;
  }

  void runInsertChain(benchmark::State& state, bool withInserts) {
    if (!BenchHelpers::engineInitOffline()) {
      state.SkipWithError("YSE::System().initOffline() failed");
      return;
    }
    {
      YSE::channel ch;
      ch.create("bench.inserts", YSE::ChannelMaster());

      YSE::DSP::MODULES::parametricEQ eq;
      YSE::DSP::MODULES::compressor comp;
      YSE::DSP::MODULES::chorus chorus;
      if (withInserts) {
        eq.gain(YSE::DSP::MODULES::EQ_LOW_SHELF, 4.0f);
        eq.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 900.f).gain(YSE::DSP::MODULES::EQ_PEAK_1, -3.0f);
        comp.threshold(-18.f).ratio(4.f).attack(10.f).release(120.f).makeup(4.f);
        chorus.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(0.6f).depth(0.4f).impact(0.4f);
        eq.link(comp);
        comp.link(chorus);
        ch.setDSP(&eq);
      }

      YSE::sound src;
      src.create(insertSource(), &ch, /*loop=*/true, /*volume=*/0.6f);
      src.play();
      pump(16);

      for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
      }
      state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);

      src.stop();
      pump(8);
      ch.setDSP(nullptr); // detach before the modules destruct
    }
    pump(32);
  }

  void BM_Engine_ChannelInsertChain_Baseline(benchmark::State& s) {
    runInsertChain(s, /*withInserts=*/false);
  }
  BENCHMARK(BM_Engine_ChannelInsertChain_Baseline);

  void BM_Engine_ChannelInsertChain_EqCompChorus(benchmark::State& s) {
    runInsertChain(s, /*withInserts=*/true);
  }
  BENCHMARK(BM_Engine_ChannelInsertChain_EqCompChorus);

  // ── send fan-in cost ─────────────────────────────────────────────────────────
  //
  // N source channels each feeding one shared plate-reverb return bus, post-fader.
  // Measures the cost of the send accumulation + the single return's DSP as the
  // fan-in count grows.

  void BM_Engine_SendFanIn(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
      state.SkipWithError("YSE::System().initOffline() failed");
      return;
    }
    const int sources = static_cast<int>(state.range(0));
    {
      YSE::channel ret;
      ret.makeReturn("bench.return");
      YSE::DSP::MODULES::plateReverb plate;
      plate.decay(0.6f).damping(6000.f).preDelay(20.f).impact(1.0f);
      ret.setDSP(&plate);

      std::vector<std::unique_ptr<YSE::channel>> chans;
      std::vector<std::unique_ptr<YSE::sound>> srcs;
      chans.reserve(sources);
      srcs.reserve(sources);
      for (int i = 0; i < sources; ++i) {
        auto c = std::make_unique<YSE::channel>();
        c->create("bench.src", YSE::ChannelMaster());
        c->send(0, ret, 0.4f);
        auto s = std::make_unique<YSE::sound>();
        s->create(insertSource(), c.get(), /*loop=*/true, /*volume=*/0.4f);
        s->play();
        chans.push_back(std::move(c));
        srcs.push_back(std::move(s));
      }
      pump(16);

      for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
      }
      state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);

      for (auto& s : srcs)
        s->stop();
      pump(8);
      for (auto& c : chans)
        c->clearSend(0);
      ret.setDSP(nullptr);
      pump(8);
    }
    pump(32);
  }
  BENCHMARK(BM_Engine_SendFanIn)->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(32);

  // ── N positioned notes (per-note 3D swarm) ───────────────────────────────────
  //
  // One synth of N sine voices with an orbit position handler, N notes held, so
  // the per-note positioning route steers and pans independent voices against the
  // listener every block. Measures the audio-thread cost of the positioned
  // (Route 2) path as the held-note count grows — the counterpart to the
  // handler-free BM_Engine_SynthVoiceScaling above.

  void BM_Engine_PositionedNotes(benchmark::State& state) {
    if (!BenchHelpers::engineInitOffline()) {
      state.SkipWithError("YSE::System().initOffline() failed");
      return;
    }
    const int voices = static_cast<int>(state.range(0));

    YSE::SYNTH::sineVoice proto;
    proto.attack(0.005f).decay(0.05f).sustain(0.8f).release(0.2f);
    YSE::SYNTH::orbitHandler handler;
    handler.radius(1.0f).velocityRadius(2.0f).rate(2.5f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, voices).positionHandler(handler);
      YSE::sound snd;
      snd.create(syn);
      if (!bringReady(syn, voices)) {
        state.SkipWithError("synth did not reach expected voice count");
        return;
      }
      snd.play();
      for (int n : chordNotes(voices))
        syn.noteOn(kMidiChannel, n, 0.8f);
      pump(16);

      for (auto _ : state) {
        YSE::System().renderOffline(kBlocksPerIter);
        benchmark::ClobberMemory();
      }
      state.SetItemsProcessed(state.iterations() * kBlocksPerIter * YSE::STANDARD_BUFFERSIZE);

      syn.allNotesOff();
      snd.stop();
      pump(8);
    }
    pump(32);
  }
  BENCHMARK(BM_Engine_PositionedNotes)->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(32);

} // namespace
