#include "yse_c/yse_instrument.h"
#include "yse_c_internal.hpp"
#include "yse_instrument_internal.hpp"

#include "../synth/samplerVoice.hpp"
#include "../dsp/fm/dx7Sysex.hpp"
#include "../implementations/logImplementation.h"
#include "../headers/enums.hpp"

#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

// The instrument-asset handles (SFZ instrument + DX7 bank) are the one place in
// the C API that must survive double-free and use-after-destroy as LOGGED
// no-ops rather than crashes (issue #178 acceptance). The engine's own create/
// destroy handles trust the caller; these do not. Both live entirely on the
// control / setup thread — loading an SFZ or a bank already allocates and reads
// files — so a mutex-guarded live-handle registry here is fine (it is NOT on
// any audio-thread path, so the c-api-extend "no mutex in RT bridges" rule does
// not apply). Every handle is registered on create and validated on every use;
// an unknown pointer is ignored and logged.

namespace {

  // Impl structs — layout owned by this TU only.
  struct SfzInstrumentImpl {
    std::shared_ptr<YSE::SYNTH::samplerInstrument> inst;
  };
  struct Dx7BankImpl {
    YSE::SYNTH::dx7Bank bank;
  };

  // Live-handle registry. Separate sets per kind so passing a bank where an
  // instrument is expected (or vice versa) is caught, not misinterpreted.
  std::mutex& registryMutex() {
    static std::mutex m;
    return m;
  }
  std::unordered_set<const void*>& liveSfz() {
    static std::unordered_set<const void*> s;
    return s;
  }
  std::unordered_set<const void*>& liveBank() {
    static std::unordered_set<const void*> s;
    return s;
  }

  void logIgnored(const char* what) {
    YSE::INTERNAL::LogImpl().emit(YSE::E_WARNING, std::string(what));
  }

  // Register a freshly created handle.
  void registerSfz(const void* p) {
    std::lock_guard<std::mutex> lk(registryMutex());
    liveSfz().insert(p);
  }
  void registerBank(const void* p) {
    std::lock_guard<std::mutex> lk(registryMutex());
    liveBank().insert(p);
  }

  // Erase on destroy; returns true if the handle was live (so the caller frees
  // it), false if it was NULL / unknown / already destroyed (logged no-op).
  bool retireSfz(const void* p) {
    if (!p) return false;
    std::lock_guard<std::mutex> lk(registryMutex());
    return liveSfz().erase(p) != 0;
  }
  bool retireBank(const void* p) {
    if (!p) return false;
    std::lock_guard<std::mutex> lk(registryMutex());
    return liveBank().erase(p) != 0;
  }

  // Validate for use; true only for a live registered handle of the right kind.
  bool isLiveSfz(const void* p) {
    if (!p) return false;
    std::lock_guard<std::mutex> lk(registryMutex());
    return liveSfz().count(p) != 0;
  }
  bool isLiveBank(const void* p) {
    if (!p) return false;
    std::lock_guard<std::mutex> lk(registryMutex());
    return liveBank().count(p) != 0;
  }

  inline SfzInstrumentImpl* toSfz(YseSfzInstrument* h) {
    return reinterpret_cast<SfzInstrumentImpl*>(h);
  }
  inline Dx7BankImpl* toBank(YseDx7Bank* h) {
    return reinterpret_cast<Dx7BankImpl*>(h);
  }

  size_t copy_string(const std::string& src, char* buf, size_t cap) {
    if (buf && cap > 0) {
      size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
      std::memcpy(buf, src.data(), n);
      buf[n] = '\0';
    }
    return src.size();
  }

} // namespace

// Cross-TU accessors (declared in yse_instrument_internal.hpp) — validate the
// handle against the registry, then hand yse_synth.cpp the engine payload.
namespace yse_c {

  std::shared_ptr<YSE::SYNTH::samplerInstrument>
  sampler_instrument_from_handle(YseSfzInstrument* h) {
    if (!isLiveSfz(h)) {
      logIgnored("yse_synth_add_voices_sampler: unknown or destroyed instrument handle (ignored)");
      return nullptr;
    }
    return toSfz(h)->inst;
  }

  const YSE::SYNTH::dx7Bank* dx7_bank_from_handle(YseDx7Bank* h) {
    if (!isLiveBank(h)) {
      logIgnored("yse_synth_fm_set_patch: unknown or destroyed bank handle (ignored)");
      return nullptr;
    }
    return &toBank(h)->bank;
  }

} // namespace yse_c

extern "C" {

// ─── SFZ sampler instrument ──────────────────────────────────────────────

YSE_C_API YseSfzInstrument* yse_sfz_load(const char* path) {
  if (!path) {
    yse_c::set_last_error("yse_sfz_load: null path");
    return nullptr;
  }
  try {
    // Load through the tested public loader on a throwaway prototype, then keep
    // its shared instrument (region table + resident PCM). Setup-thread work.
    YSE::SYNTH::samplerVoice v;
    if (!v.loadSFZ(path)) {
      yse_c::set_last_error(std::string("yse_sfz_load: no playable instrument in: ") + path);
      return nullptr;
    }
    auto* impl = new SfzInstrumentImpl();
    impl->inst = v.instrument();
    registerSfz(impl);
    return reinterpret_cast<YseSfzInstrument*>(impl);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_sfz_load: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API YseSfzInstrument* yse_sfz_load_config(const YseSamplerConfig* cfg) {
  if (!cfg || !cfg->file) {
    yse_c::set_last_error("yse_sfz_load_config: null config or file");
    return nullptr;
  }
  try {
    YSE::SYNTH::samplerConfig sc;
    if (cfg->name) sc.name(cfg->name);
    sc.file(cfg->file)
        .root(static_cast<U8>(cfg->root))
        .range(static_cast<U8>(cfg->low), static_cast<U8>(cfg->high))
        .envelope(cfg->attack, cfg->release, cfg->max_length);
    YSE::SYNTH::samplerVoice v;
    if (!v.configure(sc)) {
      yse_c::set_last_error(std::string("yse_sfz_load_config: could not build instrument from: ") +
                            cfg->file);
      return nullptr;
    }
    auto* impl = new SfzInstrumentImpl();
    impl->inst = v.instrument();
    registerSfz(impl);
    return reinterpret_cast<YseSfzInstrument*>(impl);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_sfz_load_config: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API int yse_sfz_is_valid(YseSfzInstrument* h) {
  if (!isLiveSfz(h)) return 0;
  auto& inst = toSfz(h)->inst;
  return (inst && inst->valid()) ? 1 : 0;
}

YSE_C_API void yse_sfz_destroy(YseSfzInstrument* h) {
  if (!retireSfz(h)) {
    if (h) logIgnored("yse_sfz_destroy: unknown or double-freed instrument handle (ignored)");
    return;
  }
  delete toSfz(h);
}

// ─── DX7 SysEx bank ──────────────────────────────────────────────────────

YSE_C_API YseDx7Bank* yse_dx7_import_sysex(const char* path) {
  if (!path) {
    yse_c::set_last_error("yse_dx7_import_sysex: null path");
    return nullptr;
  }
  try {
    auto* impl = new Dx7BankImpl();
    if (!YSE::SYNTH::dx7SysEx::loadBank(path, impl->bank) || impl->bank.empty()) {
      delete impl;
      yse_c::set_last_error(std::string("yse_dx7_import_sysex: could not parse bank: ") + path);
      return nullptr;
    }
    registerBank(impl);
    return reinterpret_cast<YseDx7Bank*>(impl);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_dx7_import_sysex: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API int yse_dx7_get_patch_count(YseDx7Bank* h) {
  if (!isLiveBank(h)) return 0;
  return static_cast<int>(toBank(h)->bank.size());
}

YSE_C_API size_t yse_dx7_get_patch_name(YseDx7Bank* h, int index, char* buf, size_t cap) {
  if (!isLiveBank(h) || index < 0) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(toBank(h)->bank.name(static_cast<size_t>(index)), buf, cap);
}

YSE_C_API void yse_dx7_destroy(YseDx7Bank* h) {
  if (!retireBank(h)) {
    if (h) logIgnored("yse_dx7_destroy: unknown or double-freed bank handle (ignored)");
    return;
  }
  delete toBank(h);
}

} // extern "C"
