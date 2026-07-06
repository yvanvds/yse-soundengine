#ifndef PATCHERINSERT_H_INCLUDED
#define PATCHERINSERT_H_INCLUDED

#include "dspObject.hpp"
#include "../headers/defines.hpp"

namespace YSE {
  class patcher;

  namespace DSP {

    /**
     *  @brief Runs a ``YSE::patcher`` graph as an insert effect.
     *
     *  A ``patcherInsert`` wraps a patcher so it can be attached anywhere a
     *  ``DSP::dspObject`` chain goes — ``YSE::sound::setDSP`` or
     *  ``YSE::channel::setDSP`` (issue #167). Each ``process`` call feeds the
     *  host's incoming audio to the graph's ``~adc`` objects, renders the
     *  graph, and copies the summed ``~dac`` output back over the host buffer
     *  in place. This turns a hand-patched network (a filter-delay, a custom
     *  EQ, ...) into a channel or per-sound insert.
     *
     *  Build the patcher (with at least one ``~adc`` and one ``~dac``) before
     *  attaching the insert, and keep it alive for as long as the insert is
     *  attached — the insert borrows the patcher, it does not own it.
     *
     *  ### Channel-count behaviour
     *
     *  The graph is created with a fixed channel count (``patcher::create``).
     *  When the host buffer has a different channel count than the graph:
     *  - fewer host channels than graph inputs: the extra ``~adc`` channels get
     *    no input (silent);
     *  - fewer graph outputs than host channels: the extra host channels pass
     *    through unchanged (dry) — only the channels the graph produces are
     *    replaced.
     */
    class API patcherInsert : public dspObject {
    public:
      /** @brief Wrap ``patch`` as an insert. ``patch`` must outlive this object. */
      explicit patcherInsert(YSE::patcher& patch);

      /** @brief No-op: the wrapped patcher owns its own graph and buffers. */
      void create() override;

      /** @brief Render the patcher over ``buffer`` in place. Audio thread. */
      void process(MULTICHANNELBUFFER& buffer) override;

    private:
      // Borrowed, not owned. Null is tolerated (process is a no-op).
      YSE::patcher* patch;
    };

  } // namespace DSP
} // namespace YSE

#endif // PATCHERINSERT_H_INCLUDED
