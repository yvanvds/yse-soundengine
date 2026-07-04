// Unit tests for YSE::IntrusiveForwardList (issue #194).
//
// The list replaces the std::forward_list<T*> bookkeeping the sound/channel/
// reverb managers walked and mutated on the audio callback thread. The key
// guarantees exercised here:
//   - functional parity with the forward_list idioms it replaced
//     (head-insertion order, remove, remove_if, cursor erase-during-iteration);
//   - one node can live in two independent lists via two link fields;
//   - none of the mutating operations allocate (the whole point of the change).

#include <doctest/doctest.h>

#include <vector>

#include "utils/intrusiveForwardList.hpp"
#include "support/alloc_probe.hpp"

namespace {

  struct Node {
    int id = 0;
    // Two independent intrusive links, mirroring how a real impl belongs to a
    // manager list (via one link) and its parent's child list (via another).
    Node* aNext = nullptr;
    Node* bNext = nullptr;
  };

  using ListA = YSE::IntrusiveForwardList<Node, &Node::aNext>;
  using ListB = YSE::IntrusiveForwardList<Node, &Node::bNext>;

  // Collect the ids of a list in iteration order.
  std::vector<int> ids(const ListA& list) {
    std::vector<int> out;
    for (auto it = list.begin(); it != list.end(); ++it)
      out.push_back((*it)->id);
    return out;
  }

} // namespace

TEST_SUITE("utils") {

  TEST_CASE("IntrusiveForwardList: push_front prepends (head-insertion order)") {
    Node n1{1, {}, {}}, n2{2, {}, {}}, n3{3, {}, {}};
    ListA list;
    CHECK(list.empty());
    list.push_front(&n1);
    list.push_front(&n2);
    list.push_front(&n3);
    CHECK_FALSE(list.empty());
    CHECK(ids(list) == std::vector<int>{3, 2, 1});
  }

  TEST_CASE("IntrusiveForwardList: remove unlinks head, middle, and tail nodes") {
    Node n1{1, {}, {}}, n2{2, {}, {}}, n3{3, {}, {}};

    SUBCASE("middle") {
      ListA list;
      list.push_front(&n1);
      list.push_front(&n2);
      list.push_front(&n3); // 3,2,1
      list.remove(&n2);
      CHECK(ids(list) == std::vector<int>{3, 1});
      CHECK(n2.aNext == nullptr); // detached link is cleared
    }
    SUBCASE("head") {
      ListA list;
      list.push_front(&n1);
      list.push_front(&n2);
      list.push_front(&n3); // 3,2,1
      list.remove(&n3);
      CHECK(ids(list) == std::vector<int>{2, 1});
    }
    SUBCASE("tail") {
      ListA list;
      list.push_front(&n1);
      list.push_front(&n2);
      list.push_front(&n3); // 3,2,1
      list.remove(&n1);
      CHECK(ids(list) == std::vector<int>{3, 2});
    }
    SUBCASE("absent is a no-op") {
      Node other{99, {}, {}};
      ListA list;
      list.push_front(&n1);
      list.remove(&other);
      CHECK(ids(list) == std::vector<int>{1});
    }
  }

  TEST_CASE("IntrusiveForwardList: remove_if drops matching nodes and keeps order") {
    Node n1{1, {}, {}}, n2{2, {}, {}}, n3{3, {}, {}}, n4{4, {}, {}};
    ListA list;
    list.push_front(&n1);
    list.push_front(&n2);
    list.push_front(&n3);
    list.push_front(&n4); // 4,3,2,1
    list.remove_if([](Node* n) { return n->id % 2 == 0; }); // drop evens
    CHECK(ids(list) == std::vector<int>{3, 1});
    CHECK(n2.aNext == nullptr);
    CHECK(n4.aNext == nullptr);
  }

  TEST_CASE("IntrusiveForwardList: cursor erases the current node and lands on its successor") {
    Node n1{1, {}, {}}, n2{2, {}, {}}, n3{3, {}, {}}, n4{4, {}, {}};
    ListA list;
    list.push_front(&n1);
    list.push_front(&n2);
    list.push_front(&n3);
    list.push_front(&n4); // 4,3,2,1

    // Erase ids 3 and 1 while walking once — the same erase-during-iteration
    // idiom the manager update() loops use.
    for (auto c = list.front(); c.valid();) {
      if (c.get()->id == 3 || c.get()->id == 1)
        c.erase();
      else
        c.next();
    }
    CHECK(ids(list) == std::vector<int>{4, 2});
  }

  TEST_CASE("IntrusiveForwardList: clear detaches every node without owning them") {
    Node n1{1, {}, {}}, n2{2, {}, {}};
    ListA list;
    list.push_front(&n1);
    list.push_front(&n2);
    list.clear();
    CHECK(list.empty());
    CHECK(n1.aNext == nullptr);
    CHECK(n2.aNext == nullptr);
    // Nodes still usable — clear only detaches. Re-link into a fresh list.
    ListA again;
    again.push_front(&n1);
    CHECK(ids(again) == std::vector<int>{1});
  }

  TEST_CASE("IntrusiveForwardList: one node lives in two lists via independent links") {
    Node n1{1, {}, {}}, n2{2, {}, {}};
    ListA a;
    ListB b;
    a.push_front(&n1);
    a.push_front(&n2); // a: 2,1
    b.push_front(&n2); // b: 2   (uses the other link)

    // Removing n2 from `a` must not disturb its membership in `b`.
    a.remove(&n2);
    CHECK(ids(a) == std::vector<int>{1});
    // b still holds n2 through bNext.
    int seen = 0;
    for (auto it = b.begin(); it != b.end(); ++it)
      seen += (*it)->id;
    CHECK(seen == 2);
  }

  TEST_CASE("IntrusiveForwardList: mutating operations perform no heap allocation (#194)") {
    // Pre-create the nodes OUTSIDE the probe so only the list operations are
    // measured. This is the regression guard for the whole change: the old
    // std::forward_list<T*> allocated a node per push_front / erase.
    constexpr int kNodes = 64;
    std::vector<Node> nodes(kNodes);
    for (int i = 0; i < kNodes; ++i)
      nodes[i].id = i;

    ListA list;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < kNodes; ++i)
        list.push_front(&nodes[i]);
      // Walk + erase every other node via the cursor.
      for (auto c = list.front(); c.valid();) {
        if (c.get()->id % 2 == 0)
          c.erase();
        else
          c.next();
      }
      list.remove(&nodes[1]);
      list.remove_if([](Node* n) { return n->id > 40; });
      list.clear();
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("utils")
