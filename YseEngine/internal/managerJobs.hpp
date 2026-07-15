// Shared threadpool-job templates for the channel/sound/reverb singletons.
//
// Each manager maintains a `std::forward_list` of implementation objects
// guarded by `implementationsMutex`. The setup pass claims OBJECT_CREATED
// impls for asynchronous setup; the delete pass garbage-collects impls
// whose state has reached OBJECT_DELETE. The body of both passes is the
// same across managers — only the implementation type differs. These
// templates centralize that body so the per-manager headers don't carry
// near-clone nested job classes (cpp:S4144 / duplicated-lines density).
//
// Managers opt in by:
//   1. Defining `using ImplementationType = implementationObject;`
//      inside the managerObject class.
//   2. Declaring `managerSetupJob` / `managerDeleteJob` as friends so the
//      templates can reach `implementations` and `implementationsMutex`.
//   3. Holding `managerSetupJob<managerObject>` / `managerDeleteJob<managerObject>`
//      members directly instead of nested classes.

#ifndef YSE_INTERNAL_MANAGERJOBS_HPP_INCLUDED
#define YSE_INTERNAL_MANAGERJOBS_HPP_INCLUDED

#include "threadPool.h"
#include <mutex>
#include <vector>

namespace YSE {
  namespace INTERNAL {

    template <typename Manager> class managerSetupJob : public threadPoolJob {
    public:
      explicit managerSetupJob(Manager* obj) : obj(obj) {}

      void run() override {
        using Impl = typename Manager::ImplementationType;
        std::vector<Impl*> pending;
        {
          std::scoped_lock lk(obj->implementationsMutex);
          for (auto& impl : obj->implementations) {
            if (impl.tryClaimForSetup()) {
              pending.push_back(&impl);
            }
          }
        }
        for (auto* p : pending)
          p->setup();
      }

    private:
      Manager* obj;
    };

    template <typename Manager> class managerDeleteJob : public threadPoolJob {
    public:
      explicit managerDeleteJob(Manager* obj) : obj(obj) {}

      void run() override {
        using Impl = typename Manager::ImplementationType;
        std::scoped_lock lk(obj->implementationsMutex);
        obj->implementations.remove_if(Impl::canBeDeleted);
      }

    private:
      Manager* obj;
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // YSE_INTERNAL_MANAGERJOBS_HPP_INCLUDED
