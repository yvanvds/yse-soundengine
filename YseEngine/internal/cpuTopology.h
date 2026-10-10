/*
  ==============================================================================

    cpuTopology.h
    Processor topology for render worker sizing and placement (issue #862,
    epic #856).

  ==============================================================================
*/

#ifndef CPUTOPOLOGY_H_INCLUDED
#define CPUTOPOLOGY_H_INCLUDED

#include <string>
#include <vector>
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    /** The physical cores this process may run on, and which of them are
        efficiency cores on a hybrid part (Intel P/E, AMD Zen 5/Zen 5c, ARM
        big.LITTLE).

        Everything here runs on the control thread and allocates. All of it is
        best-effort: a query that fails yields an empty topology, and callers
        fall back to the #861 policy (hardware_concurrency(), no placement).
    */
    class cpuTopology {
    public:
      struct core {
        // Logical CPUs of this core (SMT siblings) that the process may use,
        // ascending. Linux: CPU ids. Windows: processor numbers within
        // `group`.
        std::vector<Int> cpus;
        Int group = 0; // Windows processor group; 0 elsewhere
        // Relative performance: Windows EfficiencyClass + 1, Linux
        // cpu_capacity or cpufreq/cpuinfo_max_freq. 0 = unknown.
        double capacity = 0.0;
        bool efficient = false; // set by classify()
      };

      // Physical cores, ordered by their first logical CPU (group first).
      std::vector<core> cores;
      // True when the cores come in more than one performance class; the
      // lowest class is then marked `efficient`.
      bool hybrid = false;

      // Cores not marked efficient: every core on a uniform machine.
      Int performanceCores() const;

      // Mark the efficiency cores. The machine is hybrid when every core has a
      // known capacity and the weakest is below HYBRID_RATIO of the strongest;
      // the efficient cores are then those within HYBRID_RATIO of the
      // weakest. Differences smaller than that — Intel's favoured cores
      // (Turbo Boost Max 3.0), per-core boost bins — are one class. On a part
      // with three classes (prime + big + little) only the little cores count
      // as efficient.
      void classify();
      static constexpr double HYBRID_RATIO = 0.85;

      // The order render workers are placed in: performance cores first, then
      // efficiency cores, each in core order. The calling (audio) thread is
      // taken to sit on entry 0, so worker i (1-based) goes to entry
      // i % size().
      std::vector<const core*> placementOrder() const;

      // Parse a Linux-style sysfs CPU tree rooted at `root` (normally
      // "/sys/devices/system/cpu"). `allowed` (indexed by CPU id; empty means
      // every CPU) drops CPUs outside the process's affinity mask. Compiled
      // on every platform so the parser is testable against a fake tree.
      static cpuTopology fromSysfs(const std::string& root, const std::vector<bool>& allowed);

      // This machine's topology, classified. Empty when the platform cannot
      // tell. Uncached; see machine().
      static cpuTopology detect();

      // detect(), once per process.
      static const cpuTopology& machine();

      // Parse a sysfs CPU list ("0-3,8,10-11"); malformed parts are skipped.
      static std::vector<Int> parseCpuList(const std::string& list);

      // Soft placement of the calling thread on `c` (issue #862), best-effort.
      // Windows: the core's first logical CPU becomes the thread's ideal
      // processor (SetThreadIdealProcessorEx) — a preference, not an affinity.
      // Linux/Android on a hybrid part: the thread's affinity is the set of
      // every performance core's CPUs when `c` is one of them, so it stays off
      // the efficiency cluster but the OS may still move it between
      // performance cores; on a uniform machine or for an efficiency core
      // nothing is changed (a single-core mask would be a hard pin). Returns
      // true when a hint was applied and accepted.
      bool placeCurrentThread(const core& c) const;

      // "g0:4" / "cpu4" style label of a core's first CPU, for the log.
      static std::string describe(const core& c);
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // CPUTOPOLOGY_H_INCLUDED
