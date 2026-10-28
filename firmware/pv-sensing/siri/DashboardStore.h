#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dashboard {
struct Stats {
  uint32_t n = 0, invalid = 0;
  double min = NAN, max = NAN, mean = 0, m2 = 0;
  void add(double value) {
    if (!std::isfinite(value)) { ++invalid; return; }
    if (!n) min = max = value;
    if (value < min) min = value;
    if (value > max) max = value;
    ++n; double delta = value - mean; mean += delta / n; m2 += delta * (value - mean);
  }
  double stddev() const { return n > 1 ? std::sqrt(m2 / (n - 1)) : NAN; }
};
struct Channel {
  double value = NAN, raw = NAN;
  uint64_t last = 0;
  const char* status = "waiting"; // Only static firmware status strings may be passed.
  Stats stats;
};
struct Point {
  uint64_t id = 0, t = 0;
  double value = NAN, raw = NAN;
  const char* status = "waiting";
  uint8_t channel = 0;
};
template<size_t Capacity> class Store {
  static_assert(Capacity > 0, "A history buffer must contain points");
  Point points[Capacity];
  uint64_t next = 1;
  size_t count = 0;
public:
  Channel channels[4];
  uint32_t epoch = 1;
  void reset() { count = 0; ++epoch; for (auto& c : channels) c = Channel{}; }
  uint64_t cursor() const { return next - 1; }
  uint64_t oldest() const { return next - count; }
  const Point* point(uint64_t id) const {
    return id >= oldest() && id < next ? &points[(id - 1) % Capacity] : nullptr;
  }
  void add(uint8_t channel, uint64_t t, double value, double raw, const char* status) {
    if (channel >= 4) return;
    if (!std::isfinite(value)) value = NAN;
    Channel& c = channels[channel];
    c.value = value; c.raw = raw; c.last = t; c.status = status; c.stats.add(value);
    Point& p = points[(next - 1) % Capacity];
    p.id = next++; p.t = t; p.channel = channel; p.value = value; p.raw = raw; p.status = status;
    if (count < Capacity) ++count;
  }
};
} // namespace dashboard
