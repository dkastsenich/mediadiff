#include "cli/watchdog.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/fs.h"

namespace mediadiff {

namespace {

constexpr const char* kLimitVariable = "MEDIADIFF_TEST_WATCHDOG_LIMIT_MS";
constexpr const char* kStallVariable = "MEDIADIFF_TEST_STALL_LIBAV_CALL";

// A stall hook ordinal and a test limit are small decimal numbers; the bound
// keeps the arithmetic below far from overflow without rejecting anything a test
// could reasonably ask for.
constexpr std::int64_t kMaxHookNumber = 1000000000000LL;

// Parses a positive decimal integer made only of digits. False on anything else
// (empty, a sign, a space, a trailing character, zero, an absurd magnitude).
bool parse_positive(std::string_view text, std::int64_t* out) {
  if (text.empty()) {
    return false;
  }
  std::int64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::int64_t>(c - '0');
    if (value > kMaxHookNumber) {
      return false;
    }
  }
  if (value <= 0) {
    return false;
  }
  *out = value;
  return true;
}

// The final path component, split on either separator (an argv path is
// whatever the user typed on this platform).
std::string_view basename_of(std::string_view path) {
  const std::size_t cut = path.find_last_of("/\\");
  return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

mediadiff::unexpected<Error> usage_error(const char* variable, const std::string& what) {
  return mediadiff::unexpected<Error>(Error{ErrorKind::usage, std::string(variable) + " " + what});
}

std::chrono::milliseconds sampling_period(std::chrono::milliseconds limit) {
  // min(1 s, limit / 10), never below 1 ms: a test's short limit is sampled
  // finely, the production five-minute limit once a second.
  std::chrono::milliseconds period = limit / 10;
  if (period > std::chrono::milliseconds(1000)) {
    period = std::chrono::milliseconds(1000);
  }
  if (period < std::chrono::milliseconds(1)) {
    period = std::chrono::milliseconds(1);
  }
  return period;
}

}  // namespace

mediadiff::expected<WatchdogSettings, Error> resolve_watchdog_settings() {
  WatchdogSettings settings;

  const auto limit_text = getenv_utf8(kLimitVariable);
  if (limit_text.has_value() && !limit_text->empty()) {
    std::int64_t limit_ms = 0;
    if (!parse_positive(*limit_text, &limit_ms)) {
      return usage_error(kLimitVariable, "must be a positive integer number of milliseconds (a test-only variable)");
    }
    // Only ever SHORTEN the fixed limit: a larger value is ignored, so the
    // variable cannot weaken the guarantee.
    const std::chrono::milliseconds requested(limit_ms);
    if (requested < settings.limit) {
      settings.limit = requested;
    }
  }

  const auto stall_text = getenv_utf8(kStallVariable);
  if (stall_text.has_value() && !stall_text->empty()) {
    const std::string_view text = *stall_text;
    const std::size_t first = text.find(':');
    const char* shape = "must be <site>:<n>[:<basename>] (a test-only variable)";
    if (first == std::string_view::npos) {
      return usage_error(kStallVariable, shape);
    }
    const std::size_t second = text.find(':', first + 1);
    const std::string_view site_text = text.substr(0, first);
    const std::string_view count_text =
        second == std::string_view::npos ? text.substr(first + 1) : text.substr(first + 1, second - first - 1);

    StallHook hook;
    if (!parse_libav_site(site_text, &hook.site)) {
      return usage_error(kStallVariable, std::string("names an unknown libav site '") + std::string(site_text) + "'");
    }
    if (!parse_positive(count_text, &hook.nth)) {
      return usage_error(kStallVariable, shape);
    }
    if (second != std::string_view::npos) {
      hook.basename = std::string(text.substr(second + 1));
      if (hook.basename.empty()) {
        return usage_error(kStallVariable, shape);
      }
    }
    settings.stall = std::move(hook);
  }
  return settings;
}

void apply_stall_hook(const WatchdogSettings& settings, Heartbeat& heartbeat, const std::string& input_path) {
  if (!settings.stall.has_value()) {
    return;
  }
  const StallHook& hook = *settings.stall;
  if (!hook.basename.empty() && basename_of(input_path) != hook.basename) {
    return;
  }
  heartbeat.stall_nth.store(hook.nth);
  heartbeat.stall_site.store(static_cast<int>(hook.site));
}

std::string describe_trip(const WatchdogTrip& trip, std::chrono::milliseconds limit) {
  std::string limit_text;
  if (limit.count() % 1000 == 0) {
    limit_text = std::to_string(limit.count() / 1000) + " s";
  } else {
    limit_text = std::to_string(limit.count()) + " ms";
  }
  const std::string pts_text = trip.last_pts == kHeartbeatNoPts ? std::string("none") : std::to_string(trip.last_pts);
  return "'" + trip.label + "' stream " + std::to_string(trip.stream_index) + " stalled in " +
         libav_site_name(trip.site) + " for more than " + limit_text + " after pts " + pts_text;
}

void exit_after_trip(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
}

Watchdog::Watchdog(std::chrono::milliseconds limit, WatchdogTripHandler handler)
    : limit_(limit), period_(sampling_period(limit)), handler_(std::move(handler)) {}

Watchdog::~Watchdog() { stop(); }

Watchdog::SlotId Watchdog::add(Heartbeat* heartbeat, std::string label, std::size_t tag) {
  std::lock_guard<std::mutex> lock(mutex_);
  Slot slot;
  slot.id = next_id_++;
  slot.heartbeat = heartbeat;
  slot.label = std::move(label);
  slot.tag = tag;
  slot.last_seq = heartbeat->seq.load();
  slot.last_change = std::chrono::steady_clock::now();
  slots_.push_back(std::move(slot));
  return slots_.back().id;
}

void Watchdog::remove(SlotId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [id](const Slot& slot) { return slot.id == id; }),
               slots_.end());
}

void Watchdog::start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sampler_.joinable() || stopping_) {
    return;
  }
  sampler_ = std::thread([this] { run(); });
}

void Watchdog::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (sampler_.joinable()) {
    sampler_.join();
  }
}

void Watchdog::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    wake_.wait_for(lock, period_);
    if (stopping_) {
      break;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    std::vector<WatchdogTrip> trips;
    for (Slot& slot : slots_) {
      Heartbeat& beat = *slot.heartbeat;
      const std::uint64_t seq = beat.seq.load();
      const int in_call = beat.in_call.load();
      if (seq != slot.last_seq || in_call <= 0) {
        // Progress, or no libav call in flight (a wait on a rendezvous or a
        // lock is not a call): restart the stall clock.
        slot.last_seq = seq;
        slot.last_change = now;
        continue;
      }
      if (!slot.tripped && now - slot.last_change >= limit_) {
        slot.tripped = true;
        WatchdogTrip trip;
        trip.heartbeat = slot.heartbeat;
        trip.label = slot.label;
        trip.tag = slot.tag;
        trip.site = static_cast<LibavSite>(beat.site.load());
        trip.stream_index = beat.stream_index.load();
        trip.last_pts = beat.last_pts.load();
        trips.push_back(std::move(trip));
      }
    }
    if (trips.empty()) {
      continue;
    }
    // The handler runs with no lock held: dir's handler takes its pool's lock,
    // and a command's handler ends the process.
    lock.unlock();
    for (const WatchdogTrip& trip : trips) {
      handler_(trip);
    }
    lock.lock();
  }
}

WatchedRun::WatchedRun(const WatchdogSettings& settings, const std::string& primary_path,
                       const std::string* secondary_path, WatchdogTripHandler handler)
    : watchdog_(settings.limit, std::move(handler)) {
  apply_stall_hook(settings, primary_, primary_path);
  watchdog_.add(&primary_, primary_path);
  if (secondary_path != nullptr) {
    primary_.candidate_side = &secondary_;
    apply_stall_hook(settings, secondary_, *secondary_path);
    watchdog_.add(&secondary_, *secondary_path);
  }
  watchdog_.start();
}

WatchedRun::~WatchedRun() { watchdog_.stop(); }

}  // namespace mediadiff
