#include <chrono>
#include <thread>

#include <doctest.h>

#include "multitether/common/stats.h"
#include "multitether/common/time.h"

using multitether::DirectionCounters;
using multitether::kNanosPerMilli;
using multitether::MonotonicNanos;
using multitether::PathCounters;
using multitether::PeriodicTimer;
using multitether::RateSampler;
using multitether::Snapshot;
using multitether::Stopwatch;

TEST_SUITE("common.stats") {
TEST_CASE("common_stats scenario 1") {
  DirectionCounters counters;
  counters.AddFrame(1514);
  counters.AddFrame(64);
  counters.AddDroppedFull(3);
  counters.AddDroppedOversize();
  counters.AddDroppedMalformed(2);
  counters.AddIoError();

  const auto snapshot = Snapshot(counters);
  CHECK(snapshot.frames == 2);
  CHECK(snapshot.bytes == 1578);
  CHECK(snapshot.dropped_full == 3);
  CHECK(snapshot.dropped_oversize == 1);
  CHECK(snapshot.dropped_malformed == 2);
  CHECK(snapshot.io_errors == 1);
  CHECK(snapshot.TotalDropped() == 6);
}

TEST_CASE("common_stats scenario 2") {
  DirectionCounters counters;
  counters.AddBatch(10, 15140);
  counters.AddBatch(6, 9084);

  const auto snapshot = Snapshot(counters);
  CHECK(snapshot.frames == 16);
  CHECK(snapshot.bytes == 24224);
  CHECK(snapshot.batches == 2);
}

TEST_CASE("common_stats scenario 3") {
  DirectionCounters counters;
  counters.AddFrame(100);
  const auto first = Snapshot(counters);
  counters.AddFrame(200);
  counters.AddDroppedFull();
  const auto second = Snapshot(counters);

  const auto delta = second - first;
  CHECK(delta.frames == 1);
  CHECK(delta.bytes == 200);
  CHECK(delta.dropped_full == 1);
}

TEST_CASE("common_stats scenario 4: Mbps") {
  PathCounters counters;
  RateSampler sampler;

  counters.rx.AddBatch(8000, 12'500'000);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const auto report = sampler.Sample(counters);

  CHECK(report.seconds > 0.0);

  const double expected_mbps = 12'500'000.0 * 8.0 / 1'000'000.0 / report.seconds;
  CHECK(report.rx_mbps == doctest::Approx(expected_mbps).epsilon(0.01));
  CHECK(report.rx_avg_batch == doctest::Approx(8000.0));
  CHECK(report.tx_pps == doctest::Approx(0.0));
}

TEST_CASE("common_stats scenario 5") {
  const Stopwatch watch;
  const auto first = watch.Elapsed();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  const auto second = watch.Elapsed();
  CHECK(second >= first);
  CHECK(watch.ElapsedMillis() >= 1.0);
}

TEST_CASE("common_stats scenario 6") {
  PeriodicTimer timer(50 * kNanosPerMilli);

  CHECK_FALSE(timer.Expired());
  CHECK(timer.RemainingNanos(MonotonicNanos()) > 0);
  CHECK(timer.Period() == 50 * kNanosPerMilli);
}

TEST_CASE("common_stats scenario 7") {
  const multitether::Nanos now = MonotonicNanos();

  PeriodicTimer timer(10 * kNanosPerMilli, now);

  CHECK_FALSE(timer.Expired(now));
  CHECK(timer.RemainingNanos(now) > 0);

  CHECK(timer.Expired(now + 10 * kNanosPerMilli));

  CHECK_FALSE(timer.Expired(now + 10 * kNanosPerMilli));

  CHECK(timer.Expired(now + 20 * kNanosPerMilli));
}

TEST_CASE("common_stats scenario 8") {
  const multitether::Nanos now = MonotonicNanos();
  PeriodicTimer timer(10 * kNanosPerMilli, now);

  const multitether::Nanos late = now + 1000 * kNanosPerMilli;
  CHECK(timer.Expired(late));
  CHECK_FALSE(timer.Expired(late));
  CHECK_FALSE(timer.Expired(late + 9 * kNanosPerMilli));
  CHECK(timer.Expired(late + 10 * kNanosPerMilli));
}
}  // TEST_SUITE("common.stats")
