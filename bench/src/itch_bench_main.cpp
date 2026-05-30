#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "tradeflow/itch/itch.hpp"
#include "tradeflow/itch/itch_generator.hpp"
#include "tradeflow/itch/itch_reader.hpp"
#include "tradeflow/itch/pipeline.hpp"
#include "tradeflow/order.hpp"
#include "histogram.hpp"

using namespace tradeflow;
using namespace tradeflow::itch;
using bench::Histogram;

namespace {

double secs(std::chrono::steady_clock::time_point a,
            std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

inline void pace(std::chrono::steady_clock::time_point start,
                 std::uint64_t local_index, double per_producer_rate) {
  if (per_producer_rate <= 0.0) return;
  const double due_s = static_cast<double>(local_index) / per_producer_rate;
  const auto due = start + std::chrono::duration_cast<
      std::chrono::steady_clock::duration>(std::chrono::duration<double>(due_s));
  while (std::chrono::steady_clock::now() < due) { /* spin */ }
}

struct Result {
  std::string name;
  double throughput = 0;
  std::uint64_t p50 = 0, p99 = 0, p999 = 0, pmax = 0, processed = 0;
};

std::vector<ItchEvent> load_events(const std::string& file,
                                   std::uint16_t gen_symbols,
                                   std::uint64_t gen_events,
                                   std::uint64_t max_events) {
  std::vector<ItchEvent> ev;
  if (!file.empty()) {
    if (max_events) ev.reserve(max_events);
    read_itch_gz(
        file,
        [&](const std::uint8_t* p, std::uint16_t len) {
          if (max_events && ev.size() >= max_events) return;
          ItchEvent e{};
          if (parse_event(p, len, e)) ev.push_back(e);
        },
        max_events ? max_events * 4 : 0);
  } else {
    GenConfig cfg;
    cfg.num_symbols = gen_symbols;
    cfg.num_events = max_events ? max_events : gen_events;
    std::vector<std::uint8_t> buf;
    generate(cfg, buf);
    for_each_message(buf.data(), buf.size(),
                     [&](const std::uint8_t* p, std::uint16_t len) {
                       ItchEvent e{};
                       if (parse_event(p, len, e)) ev.push_back(e);
                     });
  }
  return ev;
}

Result run_sharded(const std::vector<ItchEvent>& ev, unsigned shards,
                   std::size_t ring_cap, unsigned producers, double rate) {
  const double pr = rate / producers;
  std::vector<std::unique_ptr<Histogram>> hists(shards);
  for (auto& h : hists) h = std::make_unique<Histogram>();

  ShardedBookEngine eng(shards, ring_cap,
                        [&](unsigned sh, const PipeEvent& pe) {
                          hists[sh]->add(now_ns() - pe.ingress_ns);
                        });
  eng.start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> prod;
  const std::size_t n = ev.size();
  for (unsigned p = 0; p < producers; ++p) {
    prod.emplace_back([&, p] {
      std::uint64_t local = 0;
      for (std::size_t i = p; i < n; i += producers, ++local) {
        pace(t0, local, pr);
        while (!eng.submit(ev[i])) { /* spin */ }
      }
    });
  }
  for (auto& t : prod) t.join();
  eng.drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  Histogram all;
  for (auto& h : hists) all.merge(*h);
  all.sort();
  Result r;
  r.name = "TradeFlow (sharded lock-free)";
  r.processed = eng.processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = all.percentile(0.50);
  r.p99 = all.percentile(0.99);
  r.p999 = all.percentile(0.999);
  r.pmax = all.percentile(1.0);
  return r;
}

Result run_baseline(const std::vector<ItchEvent>& ev, unsigned producers,
                    double rate) {
  const double pr = rate / producers;
  Histogram h;  // written only by the single worker thread's sink
  SequentialBookEngine eng(
      [&](unsigned, const PipeEvent& pe) { h.add(now_ns() - pe.ingress_ns); });
  eng.start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> prod;
  const std::size_t n = ev.size();
  for (unsigned p = 0; p < producers; ++p) {
    prod.emplace_back([&, p] {
      std::uint64_t local = 0;
      for (std::size_t i = p; i < n; i += producers, ++local) {
        pace(t0, local, pr);
        eng.submit(ev[i]);
      }
    });
  }
  for (auto& t : prod) t.join();
  eng.drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  h.sort();
  Result r;
  r.name = "Baseline (mutex + 1 thread)";
  r.processed = eng.processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = h.percentile(0.50);
  r.p99 = h.percentile(0.99);
  r.p999 = h.percentile(0.999);
  r.pmax = h.percentile(1.0);
  return r;
}

void print_row(const Result& r) {
  std::printf("%-32s %14.0f %10llu %10llu %10llu %10llu\n", r.name.c_str(),
              r.throughput, (unsigned long long)r.p50, (unsigned long long)r.p99,
              (unsigned long long)r.p999, (unsigned long long)r.pmax);
}

void print_table(const char* title, const Result& base, const Result& tf) {
  std::printf("\n=== %s ===\n", title);
  std::printf("%-32s %14s %10s %10s %10s %10s\n", "config", "events/sec",
              "p50(ns)", "p99(ns)", "p999(ns)", "max(ns)");
  print_row(base);
  print_row(tf);
  if (base.p99 > 0) {
    const double red = 100.0 * (1.0 - (double)tf.p99 / (double)base.p99);
    std::printf("-> P99 latency reduction: %.1f%%\n", red);
  }
  std::printf("-> Throughput speedup: %.2fx\n", tf.throughput / base.throughput);
}

}  // namespace

int main(int argc, char** argv) {
  std::string file;
  unsigned shards = 8, producers = 4;
  std::size_t ring_cap = 1 << 16;
  std::uint64_t max_events = 5'000'000;
  std::uint16_t gen_symbols = 64;
  std::uint64_t gen_events = 5'000'000;
  double rate = 2'000'000.0;
  for (int i = 1; i < argc - 1; ++i) {
    if (!std::strcmp(argv[i], "--file")) file = argv[++i];
    else if (!std::strcmp(argv[i], "--shards")) shards = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--producers")) producers = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--events")) max_events = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--ring")) ring_cap = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--rate")) rate = std::stod(argv[++i]);
    else if (!std::strcmp(argv[i], "--gen-symbols")) gen_symbols = std::stoul(argv[++i]);
  }

  std::printf("Loading events (%s)...\n",
              file.empty() ? "generated" : file.c_str());
  const auto ev = load_events(file, gen_symbols, gen_events, max_events);
  std::printf("Loaded %zu book events across up to %u shards.\n", ev.size(),
              shards);
  if (ev.empty()) {
    std::printf("No events loaded (missing file?). Exiting.\n");
    return 1;
  }

  std::printf("\n[1/2] Saturation run (max offered load)...\n");
  const Result b_sat = run_baseline(ev, producers, 0.0);
  const Result t_sat = run_sharded(ev, shards, ring_cap, producers, 0.0);
  print_table("Book reconstruction throughput @ saturation", b_sat, t_sat);

  if (rate > 0.0) {
    std::printf("\n[2/2] Paced run (%.0f events/sec sustained)...\n", rate);
    const Result b_lat = run_baseline(ev, producers, rate);
    const Result t_lat = run_sharded(ev, shards, ring_cap, producers, rate);
    char title[96];
    std::snprintf(title, sizeof(title), "Latency @ %.0f events/sec sustained",
                  rate);
    print_table(title, b_lat, t_lat);
  }
  return 0;
}
