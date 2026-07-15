/*
  ==============================================================================

    sfzModel.hpp
    SFZ v1 opcode-subset parser + RT-ready region model (issue #173).

    Implements the contract in docs/design/sfz_opcode_subset.md: an offline
    text parser (slow-pool / setup thread only) that flattens an .sfz file
    into an immutable region table, plus allocation-free region-resolution
    free functions the sampler voice (#174) calls on the audio thread.

    Nothing in this file runs on the audio thread except resolveLayers(),
    which is allocation-free and takes only integer field compares over the
    immutable table.

  ==============================================================================
*/

#ifndef SFZMODEL_HPP_INCLUDED
#define SFZMODEL_HPP_INCLUDED

#include <string>
#include <vector>

namespace YSE {
  namespace DSP {

    /// Maximum simultaneous region layers rendered per note (spec §2/§5).
    /// A (note, velocity) cell whose match set exceeds this is truncated by
    /// the deterministic priority rule in resolveLayers(); the parser warns
    /// at parse time when any cell can overflow.
    constexpr int YSE_MAX_REGION_LAYERS = 4;

    /// Sentinel for "unset / resolve from the sample file at load time"
    /// (e.g. loop points that default to the file's embedded markers, or an
    /// end frame that defaults to the last frame). The parser records intent;
    /// the sample loader (#174) resolves sentinels against the decoded file.
    constexpr long SFZ_UNSET = -1;

    /// Sample-table index meaning "this region produces silence" (sample=*silence).
    /// Distinct from a real de-duplicated sample entry.
    constexpr int SFZ_SILENCE_SAMPLE = -2;

    enum sfzLoopMode {
      SFZ_NO_LOOP, ///< Play once from offset to end, then EG release.
      SFZ_ONE_SHOT, ///< Play to completion ignoring note-off (drums).
      SFZ_LOOP_CONTINUOUS, ///< Loop while held and through the release tail.
      SFZ_LOOP_SUSTAIN, ///< Loop only while held; play out on release.
    };

    enum sfzOffMode {
      SFZ_OFF_FAST, ///< Choke via the engine steal-fade (~5 ms declick).
      SFZ_OFF_NORMAL, ///< Choke by triggering the region's own ampeg_release.
    };

    enum sfzCurve {
      SFZ_CURVE_GAIN, ///< Linear-amplitude crossfade curve.
      SFZ_CURVE_POWER, ///< Equal-power crossfade curve (SFZ default).
    };

    /// One de-duplicated sample referenced by the instrument. Regions that
    /// resolve to the same path share one entry (the memory budget counts
    /// unique files). #173 owns path resolution + de-dup; the actual PCM
    /// residency is populated by the consumer's slow-pool load (#174).
    struct sfzSample {
      std::string path; ///< Resolved absolute-or-relative path; empty for silence.
      bool silence = false; ///< True for sample=*silence (no file).
    };

    /// Fully flattened, immutable region. All hierarchy (global/master/group)
    /// has been merged at parse time; the voice never walks a chain. Fields
    /// are the RT-ready shape from spec §13 — integer compares on the audio
    /// thread, no strings, no allocation on read.
    struct sfzRegion {
      // --- selection (integer compares on the audio thread) ---
      int lokey = 0, hikey = 127; ///< Key range, 0..127.
      int lovel = 1, hivel = 127; ///< Velocity range, 1..127.
      int pitchKeycenter = 60; ///< Note that plays the sample at recorded pitch.
      int seqPosition = 1, seqLength = 1; ///< Round-robin (1/1 = always).

      // --- crossfades (NOTE_ON-time constant gains; 0 = unset) ---
      int xfinLokey = 0, xfinHikey = 0, xfoutLokey = 0, xfoutHikey = 0;
      int xfinLovel = 0, xfinHivel = 0, xfoutLovel = 0, xfoutHivel = 0;
      int xfKeycurve = SFZ_CURVE_POWER, xfVelcurve = SFZ_CURVE_POWER;

      // --- choke ---
      int chokeGroup = 0, offBy = 0; ///< 0 = none.
      int offMode = SFZ_OFF_FAST;

      // --- pitch / tuning ---
      float tuneCents = 0.0f; ///< tune / pitch, cents.
      int transposeSemis = 0; ///< transpose, semitones.
      float pitchKeytrack = 100.0f; ///< cents per key (100 = chromatic).

      // --- playback ---
      int sampleIndex =
          SFZ_SILENCE_SAMPLE; ///< Index into sfzInstrument::samples, or SFZ_SILENCE_SAMPLE.
      long offset = 0; ///< Start this many frames in.
      long endFrame = SFZ_UNSET; ///< Last playable frame; SFZ_UNSET = file end.
      int loopMode = SFZ_NO_LOOP;
      long loopStart = SFZ_UNSET; ///< SFZ_UNSET = file marker / 0.
      long loopEnd = SFZ_UNSET; ///< SFZ_UNSET = file marker / last frame.

      // --- amplitude ---
      float volumeDb = 0.0f;
      float pan = 0.0f; ///< -100..100.
      float ampVeltrack = 100.0f; ///< percent.

      // --- amp EG (seconds; sustain 0..1) ---
      float egDelay = 0.0f, egAttack = 0.0f, egHold = 0.0f, egDecay = 0.0f;
      float egSustain = 1.0f, egRelease = 0.0f;
    };

    /// The parser's output: an immutable flattened region table plus the
    /// de-duplicated sample list. Owned (shared) by the voice-group prototype;
    /// this is an engine asset, not sampler-private (spec §10).
    struct sfzInstrument {
      std::vector<sfzRegion> regions; ///< Flattened; index == file order.
      std::vector<sfzSample> samples; ///< De-duped by resolved path.
      std::string defaultPath; ///< Effective default_path at end of parse.
      std::string sourceFile; ///< Source .sfz path (for logging), if any.

      bool valid = false; ///< True when at least one region loaded.
      int droppedRegions = 0; ///< Regions dropped (missing/disabled sample).
      bool layerOverflowWarning = false; ///< A (note,vel) cell can exceed the layer bound.
    };

    // ---- Parsing (offline; slow-pool / setup thread only) -------------------

    /// Parse SFZ text into an immutable instrument. `sfzDir` is the directory
    /// of the source .sfz file (relative sample paths resolve against
    /// default_path, itself relative to sfzDir). `sourceName` is used only in
    /// log messages. Lenient: unknown/OUT opcodes are skipped-and-logged,
    /// malformed values fall back to defaults, regions with an unresolvable
    /// sample are dropped. Never throws.
    sfzInstrument parseSFZ(const std::string& text, const std::string& sfzDir,
                           const std::string& sourceName = std::string());

    /// Read an .sfz file from disk and parse it. Derives `sfzDir` from the
    /// path. If the file is unreadable/empty, returns an instrument with
    /// valid == false (the one hard failure, spec §12).
    sfzInstrument loadSFZ(const std::string& filePath);

    // ---- Region resolution (audio-thread callable; allocation-free) ---------

    /// Whether a region matches a raw (note 0..127, velocity 1..127), ignoring
    /// round-robin. Pure integer compares.
    bool regionMatches(const sfzRegion& r, int note, int velocity);

    /// Resolve the layer set that sounds for (note, velocity) on hit number
    /// `hit` (the note's round-robin counter, 0-based). Writes up to
    /// YSE_MAX_REGION_LAYERS region indices into `outIndices` (ascending file
    /// order) and returns the count (0..YSE_MAX_REGION_LAYERS). If more than
    /// the bound match, the set is truncated by the deterministic priority
    /// rule (spec §5): narrowest velocity range, then narrowest key range,
    /// then last-in-file. Allocation-free and O(regions) — safe on the audio
    /// thread over the immutable table.
    int resolveLayers(const sfzInstrument& inst, int note, int velocity, int hit,
                      int outIndices[YSE_MAX_REGION_LAYERS]);

  } // namespace DSP
} // namespace YSE

#endif // SFZMODEL_HPP_INCLUDED
