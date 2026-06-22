/*
  ==============================================================================

    yse_module.cpp
    Created: 2026-06-22

    The `yse` live-coding module (issue #126, epic #119).

    Implements every primitive locked by docs/design/live_coding_dsl.md:
    send / on / unsubscribe / latch / schedule / tick / cancel_all. The module
    is registered with the embedded interpreter via PYBIND11_EMBEDDED_MODULE,
    whose static initializer calls PyImport_AppendInittab("yse", ...) before
    Py_Initialize — so `import yse` resolves as a builtin regardless of the
    isolated PyConfig's empty sys.path.

    Threading (see the spec's "Threading model"):
      - Every binding here runs on the SCRIPT thread with the GIL held: the C
        functions are called from script code (during evaluate()), and the
        delivery hooks (onWake) are driven from ScriptRuntime::run under the GIL.
      - The one exception is the bus-subscription lambda installed by `on`: it
        fires from NamedBus::dispatch, which runs on the MAIN thread for
        audio-origin publishes and on the SCRIPT thread for script-origin
        yse.send. It must never touch Python — it only copies the value into a
        mutex-guarded queue (g_pending), which onWake later drains under the GIL
        and delivers to the Python handler. This realises the spec's "callbacks
        run once per tick, in batch, on the script thread" guarantee and the
        t -> t+1 latency for audio-origin publishes.

    All registry state (subscriptions, schedules, counters) is touched only on
    the script thread under the GIL, so the GIL serialises it — no extra lock.
    g_tick is atomic because the main thread advances it. g_pending has its own
    mutex because the bus lambda may enqueue without the GIL.

  ==============================================================================
*/

#if YSE_ENABLE_PYTHON

// The script runtime (scriptRuntime.cpp, issue #124) owns the GIL with raw
// PyGILState_Ensure/Release. pybind's default ("complex") GIL management caches
// its own thread state and, when it finds that cache empty (because we acquired
// via the C API, not pybind), creates and *attaches* a fresh thread state —
// which collides with the runtime's already-attached one ("non-NULL old thread
// state", a fatal error). This bites in pybind's own corners, notably the
// error_already_set destructor's deleter. SIMPLE_GIL_MANAGEMENT routes pybind's
// gil_scoped_acquire straight through PyGILState_Ensure, which is reentrant and
// interoperates cleanly with the runtime's raw calls. Only this TU includes
// pybind, so the definition is binary-wide and ODR-safe.
#define PYBIND11_SIMPLE_GIL_MANAGEMENT

#include <pybind11/embed.h>
#include <pybind11/eval.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dsl_runtime.h"
#include "py_traceback.h"
#include "../internal/global.h"
#include "../internal/namedBus.h"

namespace py = pybind11;

namespace YSE {
  namespace INTERNAL {
    namespace dsl {

      namespace {

        // ── DSL runtime state ─────────────────────────────────────────────
        // Monotonic engine tick. Written by the main thread (advanceTick) and
        // read from anywhere (yse.tick, schedule arithmetic) — hence atomic.
        std::atomic<long long> g_tick{0};

        // Script-thread-only state (serialised by the GIL).
        long long g_generation = 0;   // current generation; ++ per evaluation
        long long g_nextSubId = 1;    // public handle for yse.on subscriptions
        long long g_nextSchedId = 1;  // public handle for yse.schedule
        bool g_bound = false;         // is `yse` bound into __main__ yet?

        struct Subscription {
          SubHandle  busHandle;   // handle into NamedBus, for teardown
          long long  generation;  // generation tag (cancel_all granularity)
          py::object callback;    // Python handler; GIL-only
        };
        std::unordered_map<long long, Subscription> g_subs;

        struct Schedule {
          long long  fireAt;        // tick at which it matures
          long long  creationTick;  // tick when scheduled (the >guard for N=0)
          long long  generation;
          py::object fn;
          py::object args;    // py::args (a tuple)
          py::object kwargs;  // py::kwargs (a dict)
        };
        std::vector<Schedule> g_scheds;

        // Bus deliveries waiting for the script thread to dispatch them.
        struct Pending {
          long long subId;
          BusValue  value;
        };
        std::mutex           g_pendingMutex;
        std::vector<Pending> g_pending;

        constexpr const char* kSendTypeMsg =
            "yse.send: value must be int, float, str, or list of numbers";

        // Enqueue a bus delivery for later dispatch on the script thread. Runs
        // on whatever thread NamedBus::dispatch is on; never touches Python.
        void enqueuePending(long long subId, const BusValue& value) {
          std::lock_guard<std::mutex> lock(g_pendingMutex);
          g_pending.push_back(Pending{subId, value});
        }

        // BusValue -> Python. A list always arrives as list[float] (the spec's
        // only sequence type); monostate maps to None.
        py::object busValueToPy(const BusValue& value) {
          if (const int* p = std::get_if<int>(&value)) return py::int_(*p);
          if (const float* p = std::get_if<float>(&value)) return py::float_(*p);
          if (const std::string* p = std::get_if<std::string>(&value)) return py::str(*p);
          if (const std::vector<float>* p = std::get_if<std::vector<float>>(&value)) {
            py::list out;
            for (float f : *p) out.append(py::float_(f));
            return std::move(out);
          }
          return py::none();
        }

        // Python -> BusValue, enforcing the spec's value-type table. bool is
        // rejected (not coerced to int); tuple/dict/None/bytes/etc. raise
        // TypeError. List elements are coerced via float() per the spec.
        BusValue pyToBusValue(py::handle value) {
          // bool first: it subclasses int, and the spec forbids silent coercion.
          if (py::isinstance<py::bool_>(value)) {
            throw py::type_error(kSendTypeMsg);
          }
          if (py::isinstance<py::int_>(value)) {
            return BusValue{value.cast<int>()};
          }
          if (py::isinstance<py::float_>(value)) {
            return BusValue{value.cast<float>()};
          }
          if (py::isinstance<py::str>(value)) {
            return BusValue{value.cast<std::string>()};
          }
          if (py::isinstance<py::list>(value)) {
            std::vector<float> out;
            for (py::handle elem : value) {
              // float(elem) — raises TypeError for non-numeric elements, which
              // propagates as the script's traceback.
              py::object o = py::reinterpret_borrow<py::object>(elem);
              out.push_back(py::float_(o).cast<float>());
            }
            return BusValue{std::move(out)};
          }
          throw py::type_error(kSendTypeMsg);
        }

        // ── Primitive implementations ─────────────────────────────────────

        void py_send(const std::string& name, py::handle value) {
          if (name.empty()) {
            throw py::value_error("yse.send: name must not be empty");
          }
          BusValue bv = pyToBusValue(value);
          // Script thread == main-equivalent: T_GUI dispatches synchronously to
          // subscribers (the spec's "thread tag = main-equivalent").
          Global().namedBus().publish(name, bv, YSE::T_GUI);
        }

        long long py_on(const std::string& name, py::object callback) {
          if (name.empty()) {
            throw py::value_error("yse.on: name must not be empty");
          }
          if (PyCallable_Check(callback.ptr()) == 0) {
            throw py::type_error("yse.on: callback must be callable");
          }
          const long long subId = g_nextSubId++;
          // The lambda runs from NamedBus::dispatch (possibly off the script
          // thread); it must not touch Python, so it only queues the value.
          SubHandle busHandle = Global().namedBus().subscribe(
              name, [subId](const BusValue& v) { enqueuePending(subId, v); });
          g_subs.emplace(subId, Subscription{busHandle, g_generation, std::move(callback)});
          return subId;
        }

        void py_unsubscribe(long long handle) {
          auto it = g_subs.find(handle);
          if (it == g_subs.end()) return;  // unknown / stale handle: no-op
          Global().namedBus().unsubscribe(it->second.busHandle);
          g_subs.erase(it);
        }

        long long py_schedule(int ticks, py::object fn, py::args args, py::kwargs kwargs) {
          if (ticks < 0) {
            throw py::value_error("yse.schedule: ticks must be >= 0");
          }
          if (PyCallable_Check(fn.ptr()) == 0) {
            throw py::type_error("yse.schedule: fn must be callable");
          }
          const long long now = g_tick.load(std::memory_order_relaxed);
          const long long handle = g_nextSchedId++;
          g_scheds.push_back(Schedule{now + ticks, now, g_generation, std::move(fn),
                                      std::move(args), std::move(kwargs)});
          return handle;
        }

        void py_cancel_all() {
          // Strictly-older generations only: registrations from the current
          // evaluation survive (the spec's "cancel_all() at the top of a script
          // still keeps its own subsequent registrations").
          std::vector<long long> drop;
          for (auto& kv : g_subs) {
            if (kv.second.generation < g_generation) {
              Global().namedBus().unsubscribe(kv.second.busHandle);
              drop.push_back(kv.first);
            }
          }
          for (long long id : drop) g_subs.erase(id);

          const long long gen = g_generation;
          g_scheds.erase(
              std::remove_if(g_scheds.begin(), g_scheds.end(),
                             [gen](const Schedule& s) { return s.generation < gen; }),
              g_scheds.end());
        }

        // Python source for the parts most naturally written in Python: the
        // PEP 562 module __getattr__ that backs the `yse.tick` attribute, and
        // the Latch helper (a thin wrapper over on/unsubscribe). Exec'd into the
        // module namespace at import, where on/unsubscribe/_tick already live.
        constexpr const char* kBootstrap = R"PY(
def __getattr__(name):
    if name == 'tick':
        return _tick()
    raise AttributeError("module 'yse' has no attribute " + repr(name))


class Latch:
    """Caches the most recent value published to a bus address.

    Holds a subscription for as long as it is reachable; dropping the last
    reference (or calling unsubscribe()) releases it.
    """
    __slots__ = ('_handle', '_value')

    def __init__(self, name):
        self._value = None
        self._handle = on(name, self._update)

    def _update(self, value):
        self._value = value

    @property
    def value(self):
        return self._value

    def unsubscribe(self):
        unsubscribe(self._handle)

    def __del__(self):
        try:
            unsubscribe(self._handle)
        except Exception:
            pass


def latch(name):
    return Latch(name)
)PY";

      } // namespace

      // ── Engine-facing hooks (declared in dsl_runtime.h) ─────────────────

      void advanceTick() {
        g_tick.fetch_add(1, std::memory_order_relaxed);
      }

      void reset() {
        // Main thread, before the worker launches: only the counters. The
        // registries hold py::objects and are cleared on the script thread in
        // shutdown(); they are already empty at a fresh session start.
        g_tick.store(0, std::memory_order_relaxed);
        g_generation = 0;
        g_bound = false;
      }

      void beginGeneration() {
        ++g_generation;
      }

      void ensureBound() {
        if (g_bound) return;
        py::module_ yse = py::module_::import("yse");
        py::module_ main = py::module_::import("__main__");
        main.attr("yse") = yse;
        g_bound = true;
      }

      std::vector<std::string> onWake() {
        std::vector<std::string> errors;

        const long long now = g_tick.load(std::memory_order_relaxed);

        // Snapshot both work sets up front so callbacks that publish or
        // schedule during this drain are handled on the next wake, not this one
        // — the "once per tick, in batch" guarantee.
        std::vector<Schedule> due;
        {
          std::vector<Schedule> keep;
          keep.reserve(g_scheds.size());
          for (auto& s : g_scheds) {
            // fire when matured AND past the creation tick (so ticks==0 fires on
            // the *next* update, never the current one).
            if (s.fireAt <= now && now > s.creationTick) {
              due.push_back(std::move(s));
            } else {
              keep.push_back(std::move(s));
            }
          }
          g_scheds.swap(keep);
        }

        std::vector<Pending> batch;
        {
          std::lock_guard<std::mutex> lock(g_pendingMutex);
          batch.swap(g_pending);
        }

        for (auto& s : due) {
          try {
            if (s.kwargs && py::len(s.kwargs) > 0) {
              s.fn(*s.args, **s.kwargs);
            } else {
              s.fn(*s.args);
            }
          } catch (py::error_already_set& e) {
            e.restore();
            errors.push_back(formatCurrentException());
          }
        }

        for (auto& p : batch) {
          // Copy the callback out before invoking — the handler may unsubscribe
          // itself or others, mutating g_subs.
          auto it = g_subs.find(p.subId);
          if (it == g_subs.end()) continue;  // unsubscribed since enqueue
          py::object cb = it->second.callback;
          py::object value = busValueToPy(p.value);
          try {
            cb(value);
          } catch (py::error_already_set& e) {
            e.restore();
            errors.push_back(formatCurrentException());
          }
        }

        return errors;
      }

      void shutdown() {
        // GIL held: release every Python object before Py_FinalizeEx. The bus
        // is torn down moments later (global::close) and dispatches nothing
        // during shutdown, so the orphaned subscription lambdas are harmless.
        g_subs.clear();
        g_scheds.clear();
        {
          std::lock_guard<std::mutex> lock(g_pendingMutex);
          g_pending.clear();
        }
        g_bound = false;
        g_generation = 0;
      }

    } // namespace dsl
  } // namespace INTERNAL
} // namespace YSE

// The module itself. PYBIND11_EMBEDDED_MODULE's static initializer registers
// `yse` via PyImport_AppendInittab before Py_Initialize (it asserts the
// interpreter is not yet up), satisfying the spec's "registered before
// Py_Initialize" requirement without any explicit call site.
PYBIND11_EMBEDDED_MODULE(yse, m) {
  namespace dsl = YSE::INTERNAL::dsl;

  m.doc() = "YSE live-coding DSL (see docs/design/live_coding_dsl.md)";

  m.def("send", &dsl::py_send, py::arg("name"), py::arg("value"));
  m.def("on", &dsl::py_on, py::arg("name"), py::arg("callback"));
  m.def("unsubscribe", &dsl::py_unsubscribe, py::arg("handle"));
  m.def("schedule", &dsl::py_schedule);
  m.def("cancel_all", &dsl::py_cancel_all);
  // Backing accessor for the `yse.tick` attribute, surfaced through the module
  // __getattr__ defined in the bootstrap below.
  m.def("_tick", []() { return dsl::g_tick.load(std::memory_order_relaxed); });

  // Define `tick`'s __getattr__ and the Latch helper in the module namespace.
  py::exec(dsl::kBootstrap, m.attr("__dict__"));
}

#endif  // YSE_ENABLE_PYTHON
