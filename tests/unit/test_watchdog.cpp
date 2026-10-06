// 07-13-PLAN.md (D-12): the Watchdog sampler on synthetic heartbeats -- no libav,
// no process exit. A slot trips once when a call is in progress and the sequence
// has not moved for the limit; an idle slot, a moving slot and a removed slot
// never trip. Assertions run on the test's own thread only.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cli/watchdog.h"
#include "probe/heartbeat.h"

namespace {

using mediadiff::Heartbeat;
using mediadiff::LibavSite;
using mediadiff::Watchdog;
using mediadiff::WatchdogTrip;

constexpr std::chrono::milliseconds kLimit{80};

bool wait_until(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

// Collects trips under a lock (the handler runs on the sampler thread).
struct TripLog {
  std::mutex mutex;
  std::vector<WatchdogTrip> trips;
  void add(const WatchdogTrip& trip) {
    std::lock_guard<std::mutex> lock(mutex);
    trips.push_back(trip);
  }
  std::size_t size() {
    std::lock_guard<std::mutex> lock(mutex);
    return trips.size();
  }
  WatchdogTrip at(std::size_t i) {
    std::lock_guard<std::mutex> lock(mutex);
    return trips.at(i);
  }
};

}  // namespace

TEST_CASE("watchdog unit - a call in progress with a frozen sequence trips once", "[unit]") {
  Heartbeat beat;
  beat.site.store(static_cast<int>(LibavSite::video_receive));
  beat.stream_index.store(2);
  beat.last_pts.store(4242);
  beat.in_call.store(1);
  beat.seq.store(7);

  TripLog log;
  Watchdog watchdog(kLimit, [&log](const WatchdogTrip& trip) { log.add(trip); });
  watchdog.add(&beat, "clip.mp4", 5);
  watchdog.start();

  REQUIRE(wait_until([&log] { return log.size() >= 1; }));
  // Several more limits pass: still exactly one trip for the slot.
  std::this_thread::sleep_for(kLimit * 3);
  watchdog.stop();
  REQUIRE(log.size() == 1);
  const WatchdogTrip trip = log.at(0);
  CHECK(trip.heartbeat == &beat);
  CHECK(trip.label == "clip.mp4");
  CHECK(trip.tag == 5);
  CHECK(trip.site == LibavSite::video_receive);
  CHECK(trip.stream_index == 2);
  CHECK(trip.last_pts == 4242);
  CHECK(mediadiff::describe_trip(trip, kLimit) ==
        "'clip.mp4' stream 2 stalled in video_receive for more than 80 ms after pts 4242");
}

TEST_CASE("watchdog unit - an idle slot never trips, however long it waits", "[unit]") {
  Heartbeat beat;  // in_call == 0: a thread waiting on a rendezvous is not in a libav call
  TripLog log;
  Watchdog watchdog(kLimit, [&log](const WatchdogTrip& trip) { log.add(trip); });
  watchdog.add(&beat, "idle.mp4");
  watchdog.start();
  std::this_thread::sleep_for(kLimit * 6);
  watchdog.stop();
  CHECK(log.size() == 0);
}

TEST_CASE("watchdog unit - a moving sequence never trips a slot that is in a call", "[unit]") {
  Heartbeat beat;
  beat.in_call.store(1);
  std::atomic<bool> stop{false};
  std::thread mover([&beat, &stop] {
    while (!stop.load()) {
      beat.seq.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  TripLog log;
  // A generous limit against a mover that ticks every 5 ms: a loaded machine
  // that delays the mover thread by tens of milliseconds must not fail this test.
  constexpr std::chrono::milliseconds kBusyLimit{400};
  Watchdog watchdog(kBusyLimit, [&log](const WatchdogTrip& trip) { log.add(trip); });
  watchdog.add(&beat, "busy.mp4");
  watchdog.start();
  std::this_thread::sleep_for(kBusyLimit * 2);
  stop.store(true);
  mover.join();
  watchdog.stop();
  CHECK(log.size() == 0);
}

TEST_CASE("watchdog unit - a removed slot is no longer watched and slots trip independently", "[unit]") {
  Heartbeat stuck_removed;
  stuck_removed.in_call.store(1);
  Heartbeat stuck_kept;
  stuck_kept.in_call.store(1);
  TripLog log;
  Watchdog watchdog(kLimit, [&log](const WatchdogTrip& trip) { log.add(trip); });
  const Watchdog::SlotId removed = watchdog.add(&stuck_removed, "removed.mp4", 1);
  watchdog.add(&stuck_kept, "kept.mp4", 2);
  watchdog.remove(removed);
  watchdog.start();
  REQUIRE(wait_until([&log] { return log.size() >= 1; }));
  std::this_thread::sleep_for(kLimit * 3);
  watchdog.stop();
  REQUIRE(log.size() == 1);
  CHECK(log.at(0).label == "kept.mp4");
  CHECK(log.at(0).tag == 2);
}

TEST_CASE("watchdog unit - the limit is the fixed constant and describe_trip words whole seconds", "[unit]") {
  CHECK(mediadiff::kDecodeWatchdogStallSeconds == 300);
  mediadiff::WatchdogSettings settings;
  CHECK(settings.limit == std::chrono::seconds(300));

  WatchdogTrip trip;
  trip.label = "a.mkv";
  trip.stream_index = 0;
  trip.site = LibavSite::read_frame;
  // No timestamp yet.
  CHECK(mediadiff::describe_trip(trip, std::chrono::seconds(300)) ==
        "'a.mkv' stream 0 stalled in read_frame for more than 300 s after pts none");
}
