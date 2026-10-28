// Deterministic statistics/cache fixtures, never experimental measurements.
#include "../../firmware/pv-sensing/siri/DashboardStore.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  dashboard::Store<4> store;
  store.add(0, 100, 1, 10, "ok_uncalibrated");
  store.add(0, 200, 3, 30, "ok_uncalibrated");
  store.add(0, 300, NAN, NAN, "timeout");
  const auto& a = store.channels[0];
  assert(a.stats.n == 2 && a.stats.invalid == 1);
  assert(a.stats.mean == 2 && a.stats.min == 1 && a.stats.max == 3);
  assert(std::abs(a.stats.stddev() - std::sqrt(2.0)) < 1e-10);
  assert(!std::isfinite(a.value)); // Fault must clear last plotted value, not retain stale success.
  store.add(1, 400, 0, 0, "scale_unconfirmed");
  store.add(2, 500, INFINITY, 2, "out_of_range");
  assert(store.oldest() == 2 && store.cursor() == 5);
  assert(store.point(1) == nullptr && store.point(2)->t == 200);
  assert(store.channels[1].stats.n == 1 && store.channels[1].value == 0);
  assert(store.channels[2].stats.invalid == 1);
  store.reset();
  assert(store.epoch == 2 && store.cursor() == 5 && store.oldest() == 6);
  assert(store.channels[0].stats.n == 0 && !std::isfinite(store.channels[0].value));
  store.add(3, 600, 24, 8390, "reference_uncompensated");
  assert(store.point(6)->channel == 3 && store.point(6)->raw == 8390);
  assert(!std::isfinite(store.channels[3].stats.stddev()));
  std::puts("dashboard cache/statistics: passed");
}
