/*
  ==============================================================================

    cpuTopology.cpp
    Processor topology for render worker sizing and placement (issue #862,
    epic #856).

  ==============================================================================
*/

#include "cpuTopology.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) // includes Android
#include <sched.h>
#endif

namespace {
  // First line of a small text file; empty if it cannot be read.
  std::string readLine(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (in) std::getline(in, line);
    return line;
  }

  double readNumber(const std::string& path) {
    const std::string text = readLine(path);
    if (text.empty()) return 0.0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return (end == text.c_str() || value < 0.0) ? 0.0 : value;
  }

  // Upper bound on the CPU ids scanned when sysfs has no "possible" file.
  constexpr Int SYSFS_SCAN_LIMIT = 1024;
} // namespace

std::vector<Int> YSE::INTERNAL::cpuTopology::parseCpuList(const std::string& list) {
  std::vector<Int> cpus;
  std::size_t pos = 0;
  while (pos < list.size()) {
    std::size_t comma = list.find(',', pos);
    if (comma == std::string::npos) comma = list.size();
    const std::string part = list.substr(pos, comma - pos);
    pos = comma + 1;
    const char* text = part.c_str();
    char* end = nullptr;
    const long first = std::strtol(text, &end, 10);
    if (end == text || first < 0) continue;
    long last = first;
    if (*end == '-') {
      const char* second = end + 1;
      last = std::strtol(second, &end, 10);
      if (end == second || last < first) continue;
    }
    for (long cpu = first; cpu <= last && cpu < SYSFS_SCAN_LIMIT * 4; ++cpu)
      cpus.push_back(static_cast<Int>(cpu));
  }
  std::sort(cpus.begin(), cpus.end());
  cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
  return cpus;
}

Int YSE::INTERNAL::cpuTopology::performanceCores() const {
  Int count = 0;
  for (const core& c : cores)
    if (!c.efficient) ++count;
  return count;
}

void YSE::INTERNAL::cpuTopology::classify() {
  hybrid = false;
  for (core& c : cores)
    c.efficient = false;
  if (cores.size() < 2) return;
  double lowest = cores.front().capacity;
  double highest = lowest;
  for (const core& c : cores) {
    // One unknown capacity and the machine is treated as uniform: guessing
    // wrong would move workers off cores that may be the fast ones.
    if (c.capacity <= 0.0) return;
    lowest = std::min(lowest, c.capacity);
    highest = std::max(highest, c.capacity);
  }
  if (lowest >= HYBRID_RATIO * highest) return;
  hybrid = true;
  const double efficientUpTo = lowest / HYBRID_RATIO;
  for (core& c : cores)
    c.efficient = c.capacity <= efficientUpTo;
}

std::vector<const YSE::INTERNAL::cpuTopology::core*>
YSE::INTERNAL::cpuTopology::placementOrder() const {
  std::vector<const core*> order;
  order.reserve(cores.size());
  for (const core& c : cores)
    if (!c.efficient) order.push_back(&c);
  for (const core& c : cores)
    if (c.efficient) order.push_back(&c);
  return order;
}

YSE::INTERNAL::cpuTopology YSE::INTERNAL::cpuTopology::fromSysfs(const std::string& root,
                                                                 const std::vector<bool>& allowed) {
  cpuTopology topo;
  const std::vector<Int> possible = parseCpuList(readLine(root + "/possible"));
  const Int limit = possible.empty() ? SYSFS_SCAN_LIMIT : possible.back() + 1;
  const auto isAllowed = [&allowed](Int cpu) {
    return allowed.empty() || (cpu < static_cast<Int>(allowed.size()) && allowed[cpu]);
  };

  // Keyed by the core's lowest sibling, so SMT siblings merge into one core
  // whatever the architecture numbers core_id / physical_package_id as
  // (arm64 repeats core_id per cluster; newer kernels report package -1).
  std::map<Int, core> byCore;
  for (Int cpu = 0; cpu < limit; ++cpu) {
    if (!isAllowed(cpu)) continue;
    const std::string base = root + "/cpu" + std::to_string(cpu);
    std::string siblings = readLine(base + "/topology/core_cpus_list");
    if (siblings.empty()) siblings = readLine(base + "/topology/thread_siblings_list");
    if (siblings.empty()) continue; // offline, or not a CPU
    std::vector<Int> ids = parseCpuList(siblings);
    ids.push_back(cpu);
    const Int key = *std::min_element(ids.begin(), ids.end());

    double capacity = readNumber(base + "/cpu_capacity");
    if (capacity <= 0.0) capacity = readNumber(base + "/cpufreq/cpuinfo_max_freq");

    core& c = byCore[key];
    c.cpus.push_back(cpu);
    // A core's capacity is its best sibling's (they should agree); unknown
    // stays unknown only when no sibling reports one.
    c.capacity = std::max(c.capacity, capacity);
  }
  for (auto& entry : byCore)
    topo.cores.push_back(std::move(entry.second));
  topo.classify();
  return topo;
}

YSE::INTERNAL::cpuTopology YSE::INTERNAL::cpuTopology::detect() {
#if defined(_WIN32)
  cpuTopology topo;
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (bytes == 0) return topo;
  std::vector<unsigned char> buffer(bytes);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bytes)) return topo;

  // The process affinity mask describes one processor group only (and is 0
  // when the process spans several): filter by it only on a single-group
  // machine, where it is unambiguous.
  DWORD_PTR processMask = 0;
  DWORD_PTR systemMask = 0;
  if (GetActiveProcessorGroupCount() != 1 ||
      !GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask))
    processMask = 0;

  for (DWORD offset = 0; offset < bytes;) {
    auto* entry =
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
    if (entry->Size == 0) break;
    offset += entry->Size;
    if (entry->Relationship != RelationProcessorCore) continue;
    const PROCESSOR_RELATIONSHIP& p = entry->Processor;
    if (p.GroupCount == 0) continue;
    const GROUP_AFFINITY& g = p.GroupMask[0];
    KAFFINITY mask = g.Mask;
    if (processMask != 0) mask &= processMask;
    if (mask == 0) continue; // not ours to run on
    core c;
    c.group = g.Group;
    for (Int bit = 0; bit < static_cast<Int>(sizeof(KAFFINITY) * 8); ++bit)
      if ((mask >> bit) & 1u) c.cpus.push_back(bit);
    c.capacity = static_cast<double>(p.EfficiencyClass) + 1.0;
    topo.cores.push_back(std::move(c));
  }
  std::sort(topo.cores.begin(), topo.cores.end(), [](const core& a, const core& b) {
    return a.group != b.group ? a.group < b.group : a.cpus.front() < b.cpus.front();
  });
  topo.classify();
  return topo;
#elif defined(__linux__)
  std::vector<bool> allowed;
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) == 0) {
    allowed.assign(CPU_SETSIZE, false);
    for (Int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
      allowed[cpu] = CPU_ISSET(cpu, &set) != 0;
  }
  return fromSysfs("/sys/devices/system/cpu", allowed);
#else
  return cpuTopology();
#endif
}

const YSE::INTERNAL::cpuTopology& YSE::INTERNAL::cpuTopology::machine() {
  static const cpuTopology topo = detect();
  return topo;
}

bool YSE::INTERNAL::cpuTopology::placeCurrentThread(const core& c) const {
  if (c.cpus.empty()) return false;
#if defined(_WIN32)
  PROCESSOR_NUMBER ideal{};
  ideal.Group = static_cast<WORD>(c.group);
  ideal.Number = static_cast<BYTE>(c.cpus.front());
  return SetThreadIdealProcessorEx(GetCurrentThread(), &ideal, nullptr) != 0;
#elif defined(__linux__)
  if (!hybrid || c.efficient) return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  for (const core& other : cores) {
    if (other.efficient) continue;
    for (Int cpu : other.cpus)
      if (cpu < CPU_SETSIZE) CPU_SET(cpu, &set);
  }
  return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
  return false;
#endif
}

std::string YSE::INTERNAL::cpuTopology::describe(const core& c) {
  if (c.cpus.empty()) return "?";
  std::string label;
  if (c.group != 0) label = "g" + std::to_string(c.group) + ":";
  label += "cpu" + std::to_string(c.cpus.front());
  if (c.efficient) label += "(e)";
  return label;
}
