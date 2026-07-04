/*
  ==============================================================================

    intrusiveForwardList.hpp
    Created: for issue #194 — zero-allocation manager bookkeeping.

  ==============================================================================
*/

#ifndef YSE_UTILS_INTRUSIVEFORWARDLIST_HPP_INCLUDED
#define YSE_UTILS_INTRUSIVEFORWARDLIST_HPP_INCLUDED

namespace YSE {

  // A singly-linked forward list whose link pointer lives *inside* the element
  // type `T` (an "intrusive" list). Because the node storage is embedded in the
  // element — which is already owned/allocated elsewhere — none of the mutating
  // operations here touch the heap. This replaces the `std::forward_list<T*>`
  // bookkeeping in the sound/channel/reverb managers and in
  // channelImplementation, all of which are walked and mutated on the audio
  // callback thread and were the source of per-tick malloc/free node churn
  // (issue #194).
  //
  // `NextField` is a pointer-to-member naming the `T*` link inside `T`. An
  // element may belong to at most one list per link field at a time — e.g. a
  // sound impl uses one link for the manager's toLoad/inUse list (mutually
  // exclusive membership) and a second link for its parent channel's `sounds`
  // list. The list does NOT own its elements: clear() only detaches nodes; it
  // never destroys them.
  //
  // The list is intentionally non-copyable (a link is a single embedded slot,
  // so a node cannot live in two copies of a list at once).
  template <typename T, T* T::* NextField> class IntrusiveForwardList {
  public:
    IntrusiveForwardList() = default;
    IntrusiveForwardList(const IntrusiveForwardList&) = delete;
    IntrusiveForwardList& operator=(const IntrusiveForwardList&) = delete;

    bool empty() const noexcept {
      return head_ == nullptr;
    }

    // Prepend `node`. Matches std::forward_list::push_front ordering so the
    // head-insertion semantics the managers relied on are preserved.
    void push_front(T* node) noexcept {
      node->*NextField = head_;
      head_ = node;
    }

    // Detach every node without destroying it. Elements are owned by the
    // canonical `implementations` list (or equivalent), not by this list.
    void clear() noexcept {
      T* node = head_;
      while (node != nullptr) {
        T* next = node->*NextField;
        node->*NextField = nullptr;
        node = next;
      }
      head_ = nullptr;
    }

    // Unlink the first node equal to `value`. No-op if it is not present.
    void remove(T* value) noexcept {
      for (T** link = &head_; *link != nullptr; link = &((*link)->*NextField)) {
        if (*link == value) {
          T* node = *link;
          *link = node->*NextField;
          node->*NextField = nullptr;
          return;
        }
      }
    }

    // Unlink every node for which pred(node) is true. `Pred` is invoked with a
    // `T*`, matching the old std::forward_list<T*>::remove_if predicates.
    template <typename Pred> void remove_if(Pred pred) {
      T** link = &head_;
      while (*link != nullptr) {
        T* node = *link;
        if (pred(node)) {
          *link = node->*NextField;
          node->*NextField = nullptr;
        } else {
          link = &(node->*NextField);
        }
      }
    }

    // Read-only forward iterator. Dereferences to `T*` so existing `(*it)->foo()`
    // call sites that used `std::forward_list<T*>` compile unchanged.
    class iterator {
    public:
      explicit iterator(T* node) noexcept : node_(node) {}
      T* operator*() const noexcept {
        return node_;
      }
      iterator& operator++() noexcept {
        node_ = node_->*NextField;
        return *this;
      }
      iterator operator++(int) noexcept {
        iterator prev(*this);
        node_ = node_->*NextField;
        return prev;
      }
      bool operator==(const iterator& other) const noexcept {
        return node_ == other.node_;
      }
      bool operator!=(const iterator& other) const noexcept {
        return node_ != other.node_;
      }

    private:
      T* node_;
    };

    iterator begin() const noexcept {
      return iterator(head_);
    }
    iterator end() const noexcept {
      return iterator(nullptr);
    }

    // Mutating traversal that supports erase-during-iteration, replacing the
    // std::forward_list before_begin()/erase_after(previous) idiom. Ordering is
    // preserved: get() yields the current node, next() advances, and erase()
    // unlinks the current node and leaves the cursor referring to its
    // successor (so the caller must NOT also call next() after an erase).
    //
    //   for (auto c = list.cursor(); c.valid();) {
    //     T* p = c.get();
    //     if (drop) c.erase(); else c.next();
    //   }
    class cursor {
    public:
      explicit cursor(T** link) noexcept : link_(link) {}
      bool valid() const noexcept {
        return *link_ != nullptr;
      }
      T* get() const noexcept {
        return *link_;
      }
      void next() noexcept {
        link_ = &((*link_)->*NextField);
      }
      void erase() noexcept {
        T* node = *link_;
        *link_ = node->*NextField;
        node->*NextField = nullptr;
      }

    private:
      T** link_;
    };

    cursor front() noexcept {
      return cursor(&head_);
    }

  private:
    T* head_ = nullptr;
  };

} // namespace YSE

#endif // YSE_UTILS_INTRUSIVEFORWARDLIST_HPP_INCLUDED
