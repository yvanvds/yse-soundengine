// Tests for YSE::DSP::signalBus (YseEngine/dsp/routing/signalBus.hpp), the
// named signal bus behind ~send / ~receive (issue #1136): the registry, the
// block a receiver hears from a sender of another context (exactly the
// previous one) and of its own context (the latest, this block's or the
// last), fan-in summing, a sender that stops, clear, rebinding and what it
// retires, short blocks, allocation freedom, and a writer and reader on two
// threads with a control thread rebinding meanwhile (the TSan case).

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/routing/signalBus.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::buffer;
using YSE::DSP::signalBus;
using YSE::DSP::signalBusReceiver;
using YSE::DSP::signalBusSender;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A block that tells blocks and samples apart: block b, sample i.
  void Fill(buffer& b, float block) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = block * 1000.f + static_cast<float>(i);
  }

  bool Holds(buffer& b, float block) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      if (b.getPtr()[i] != block * 1000.f + static_cast<float>(i)) return false;
    }
    return true;
  }

  bool Constant(buffer& b, float value) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      if (b.getPtr()[i] != value) return false;
    }
    return true;
  }

  // Two distinct contexts, as two patchers would pass.
  int contextA = 0;
  int contextB = 0;
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("signalBus: the registry finds one bus per name (#1136)") {
    CHECK(signalBus::find("sb1136.registry").get() == nullptr);
    std::shared_ptr<signalBus> a = signalBus::acquire("sb1136.registry");
    REQUIRE(a.get() != nullptr);
    CHECK(a->name() == "sb1136.registry");
    CHECK(signalBus::acquire("sb1136.registry").get() == a.get());
    CHECK(signalBus::find("sb1136.registry").get() == a.get());
    CHECK(signalBus::acquire("sb1136.other").get() != a.get());
    const std::size_t live = signalBus::count();
    CHECK(live >= 1);
    a.reset();
    // Nobody holds it any more: gone, and a new acquire makes a fresh one.
    CHECK(signalBus::find("sb1136.registry").get() == nullptr);
    CHECK(signalBus::count() < live);
    CHECK(signalBus::block() >= 1);
  }

  TEST_CASE("signalBus: another context hears exactly the previous block (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.previous");
    signalBusSender send;
    signalBusReceiver receive;
    send.bind(bus);
    receive.bind(bus);
    CHECK(bus->senders() == 1);
    buffer in(N);

    // Read before and after the write in the same block: neither hears it.
    Fill(in, 1.f);
    CHECK(Constant(receive.read(&contextB), 0.f));
    send.write(in, &contextA);
    CHECK(Constant(receive.read(&contextB), 0.f));

    // Next block: the block before, whichever runs first.
    signalBus::advance();
    Fill(in, 2.f);
    send.write(in, &contextA);
    CHECK(Holds(receive.read(&contextB), 1.f));
    signalBus::advance();
    CHECK(Holds(receive.read(&contextB), 2.f));
    Fill(in, 3.f);
    send.write(in, &contextA);
    CHECK(Holds(receive.read(&contextB), 2.f));

    // A null context is no context: the same rule, for both ends.
    CHECK(Constant(receive.read(nullptr), 0.f) == false);
    CHECK(Holds(receive.read(nullptr), 2.f));

    // A sender that stops writing is heard for one more block, then not.
    signalBus::advance();
    CHECK(Holds(receive.read(&contextB), 3.f));
    signalBus::advance();
    CHECK(Constant(receive.read(&contextB), 0.f));
    CHECK(Constant(receive.output(), 0.f));
  }

  TEST_CASE("signalBus: the same context hears the latest block (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.latest");
    signalBusSender send;
    signalBusReceiver receive;
    send.bind(bus);
    receive.bind(bus);
    buffer in(N);

    // The receiver after the sender: this block.
    Fill(in, 1.f);
    send.write(in, &contextA);
    CHECK(Holds(receive.read(&contextA), 1.f));

    // The receiver before the sender (a feedback loop): the last block, then
    // this one once the sender has run.
    signalBus::advance();
    CHECK(Holds(receive.read(&contextA), 1.f));
    Fill(in, 2.f);
    send.write(in, &contextA);
    CHECK(Holds(receive.read(&contextA), 2.f));

    // Without the clock (a program that renders alone and never advances):
    // the same, block after block.
    Fill(in, 3.f);
    send.write(in, &contextA);
    CHECK(Holds(receive.read(&contextA), 3.f));

    // Older than the last block is not heard.
    signalBus::advance();
    signalBus::advance();
    CHECK(Constant(receive.read(&contextA), 0.f));
  }

  TEST_CASE("signalBus: several senders sum, each by its own context (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.fanin");
    signalBusSender a;
    signalBusSender b;
    signalBusSender c;
    signalBusReceiver receive;
    a.bind(bus);
    b.bind(bus);
    c.bind(bus);
    receive.bind(bus);
    CHECK(bus->senders() == 3);
    buffer one(N);
    buffer two(N);
    buffer four(N);
    one = 1.f;
    two = 2.f;
    four = 4.f;

    a.write(one, &contextA);
    b.write(two, &contextA);
    c.write(four, &contextB);
    // Same context as a and b: theirs now; c's from the previous block, none.
    CHECK(Constant(receive.read(&contextA), 3.f));
    signalBus::advance();
    // Next block, before anyone writes: a and b's last block, c's previous.
    CHECK(Constant(receive.read(&contextA), 7.f));
    CHECK(Constant(receive.read(&contextB), 7.f));
    a.write(four, &contextA);
    CHECK(Constant(receive.read(&contextA), 2.f + 4.f + 4.f));
  }

  TEST_CASE("signalBus: clear sends one block of silence (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.clear");
    signalBusSender send;
    signalBusReceiver receive;
    send.bind(bus);
    receive.bind(bus);
    buffer in(N);
    in = 0.5f;
    send.write(in, &contextA);
    CHECK(Constant(receive.read(&contextA), 0.5f));
    send.clear();
    send.write(in, &contextA);
    CHECK(Constant(receive.read(&contextA), 0.f));
    send.write(in, &contextA);
    CHECK(Constant(receive.read(&contextA), 0.5f));
  }

  TEST_CASE("signalBus: a shorter block is summed over its length (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.short");
    signalBusSender send;
    signalBusReceiver receive;
    send.bind(bus);
    receive.bind(bus);
    buffer in(N / 2);
    in = 1.f;
    send.write(in, &contextA);
    buffer& out = receive.read(&contextA);
    REQUIRE(out.getLength() == N);
    CHECK(out.getPtr()[0] == 1.f);
    CHECK(out.getPtr()[N / 2 - 1] == 1.f);
    CHECK(out.getPtr()[N / 2] == 0.f);
    CHECK(out.getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("signalBus: rebinding moves an endpoint and retires what it let go (#1136)") {
    std::shared_ptr<signalBus> x = signalBus::acquire("sb1136.x");
    std::shared_ptr<signalBus> y = signalBus::acquire("sb1136.y");
    signalBusSender send;
    signalBusReceiver hearX;
    signalBusReceiver hearY;
    send.bind(x);
    hearX.bind(x);
    hearY.bind(y);
    buffer in(N);
    in = 1.f;
    send.write(in, &contextA);
    CHECK(Constant(hearX.read(&contextA), 1.f));
    CHECK(Constant(hearY.read(&contextA), 0.f));

    send.bind(y);
    CHECK(send.bus().get() == y.get());
    CHECK(x->senders() == 0);
    CHECK(y->senders() == 1);
    send.write(in, &contextA);
    CHECK(Constant(hearX.read(&contextA), 0.f));
    CHECK(Constant(hearY.read(&contextA), 1.f));
    // Binding again to the same bus changes nothing.
    send.bind(y);
    CHECK(y->senders() == 1);

    // The receiver follows its own binding.
    hearX.bind(y);
    CHECK(hearX.bus().get() == y.get());
    CHECK(Constant(hearX.read(&contextA), 1.f));
    hearX.bind(nullptr);
    CHECK(Constant(hearX.read(&contextA), 0.f));

    // What the binds let go waits two blocks, then collect() frees it.
    CHECK(signalBus::retired() > 0);
    signalBus::advance();
    signalBus::collect();
    CHECK(signalBus::retired() > 0);
    signalBus::advance();
    signalBus::collect();
    CHECK(signalBus::retired() == 0);

    // A sender that goes away leaves its bus.
    {
      signalBusSender temporary;
      temporary.bind(y);
      CHECK(y->senders() == 2);
    }
    CHECK(y->senders() == 1);
    send.bind(nullptr);
    CHECK(y->senders() == 0);
    CHECK(send.bus().get() == nullptr);
    send.write(in, &contextA); // unbound: nothing
    CHECK(Constant(hearY.read(&contextA), 0.f));
  }

  TEST_CASE("signalBus: write, read and advance allocate nothing (#1136)") {
    std::shared_ptr<signalBus> bus = signalBus::acquire("sb1136.alloc");
    signalBusSender a;
    signalBusSender b;
    signalBusReceiver receive;
    a.bind(bus);
    b.bind(bus);
    receive.bind(bus);
    buffer in(N);
    in = 0.25f;
    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 8; ++block) {
        signalBus::advance();
        receive.read(&contextA);
        a.write(in, &contextA);
        b.write(in, &contextB);
        a.clear();
        receive.read(&contextB);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("signalBus: a writer and a reader on two threads, rebound meanwhile (#1136)") {
    // The shape of two patchers on two render workers: every block both run
    // at once, then the clock advances while neither does. A control thread
    // rebinds a second sender and the receiver between two buses all along,
    // and collects. The reader must hear exactly the writer's previous block
    // on the bus it is bound to; under TSan nothing may race.
    std::shared_ptr<signalBus> main = signalBus::acquire("sb1136.threads");
    std::shared_ptr<signalBus> side = signalBus::acquire("sb1136.threads.side");
    signalBusSender writer;
    signalBusSender extra;
    signalBusReceiver reader;
    writer.bind(main);
    reader.bind(main);
    std::atomic<bool> stop{false};
    std::thread control([&] {
      bool flip = false;
      while (!stop.load(std::memory_order_acquire)) {
        extra.bind(flip ? side : main);
        flip = !flip;
        signalBus::collect();
        std::this_thread::yield();
      }
      extra.bind(nullptr);
    });

    constexpr int blocks = 200;
    int heard = 0;
    int wrong = 0;
    buffer in(N);
    for (int block = 0; block < blocks; ++block) {
      const float value = static_cast<float>(block + 1);
      std::thread write([&] {
        in = value;
        writer.write(in, &contextA);
      });
      std::thread read([&] {
        buffer& out = reader.read(&contextB);
        // Never this block's value; the last block's (the writer's), plus
        // nothing else: `extra` never writes.
        if (block > 0) {
          if (out.getPtr()[0] == value - 1.f && out.getPtr()[N - 1] == value - 1.f) {
            heard++;
          } else {
            wrong++;
          }
        }
      });
      write.join();
      read.join();
      signalBus::advance();
    }
    stop.store(true, std::memory_order_release);
    control.join();
    CHECK(wrong == 0);
    CHECK(heard == blocks - 1);
  }

} // TEST_SUITE("dsp")
