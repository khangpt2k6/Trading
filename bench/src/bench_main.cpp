#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "tradeflow/baseline_engine.hpp"
#include "tradeflow/order.hpp"
#include "tradeflow/symbol_worker.hpp"
#include "histogram.hpp"
#include "workload.hpp"

using namespace tradeflow;
using bench::Histogram;
using bench::WorkloadConfig;

namespace {

double secs(std::chrono::steady_clock::time_point a,
            std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double>(b - a).count();
}

// Open-loop pacing: busy-wait until this producer's `local_index`-th order is
// due, given a per-producer injection rate (orders/sec). rate <= 0 disables
// pacing (max-throughput / saturation mode).
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

Result run_tradeflow(const std::vector<Order>& orders, std::uint32_t num_symbols,
                     std::size_t ring_cap, unsigned producer_threads,
                     double rate) {
  const double per_producer_rate = rate / producer_threads;
  std::vector<std::unique_ptr<Histogram>> hists(num_symbols);
  for (auto& h : hists) h = std::make_unique<Histogram>();

  std::vector<std::unique_ptr<SymbolWorker>> workers;
  workers.reserve(num_symbols);
  for (std::uint32_t s = 0; s < num_symbols; ++s) {
    Histogram* h = hists[s].get();
    workers.emplace_back(std::make_unique<SymbolWorker>(
        s, ring_cap, [h](const Order& o, const BookDelta&) {
          h->add(now_ns() - o.ts_ingress);   // ingress -> match latency
        }));
  }
  for (auto& w : workers) w->start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> producers;
  const std::size_t n = orders.size();
  for (unsigned p = 0; p < producer_threads; ++p) {
    producers.emplace_back([&, p] {
      std::uint64_t local = 0;
      for (std::size_t i = p; i < n; i += producer_threads, ++local) {
        pace(t0, local, per_producer_rate);
        Order o = orders[i];
        o.ts_ingress = now_ns();
        while (!workers[o.symbol]->submit(o)) {}
      }
    });
  }
  for (auto& t : producers) t.join();
  for (auto& w : workers) w->drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  Histogram all;
  for (auto& h : hists) all.merge(*h);
  all.sort();

  Result r;
  r.name = "TradeFlow (lock-free, thread/symbol)";
  for (auto& w : workers) r.processed += w->processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = all.percentile(0.50);
  r.p99 = all.percentile(0.99);
  r.p999 = all.percentile(0.999);
  r.pmax = all.percentile(1.0);
  return r;
}

Result run_baseline(const std::vector<Order>& orders, std::uint32_t num_symbols,
                    unsigned producer_threads, double rate) {
  const double per_producer_rate = rate / producer_threads;
  Histogram h;
  std::mutex hmu;
  BaselineEngine eng(num_symbols, [&](const Order& o, const BookDelta&) {
    const std::uint64_t lat = now_ns() - o.ts_ingress;
    std::lock_guard<std::mutex> lk(hmu);
    h.add(lat);
  });
  eng.start();

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> producers;
  const std::size_t n = orders.size();
  for (unsigned p = 0; p < producer_threads; ++p) {
    producers.emplace_back([&, p] {
      std::uint64_t local = 0;
      for (std::size_t i = p; i < n; i += producer_threads, ++local) {
        pace(t0, local, per_producer_rate);
        Order o = orders[i];
        o.ts_ingress = now_ns();
        eng.submit(o);
      }
    });
  }
  for (auto& t : producers) t.join();
  eng.drain_and_stop();
  const auto t1 = std::chrono::steady_clock::now();

  h.sort();
  Result r;
  r.name = "Baseline (mutex + blocking queue)";
  r.processed = eng.processed();
  r.throughput = r.processed / secs(t0, t1);
  r.p50 = h.percentile(0.50);
  r.p99 = h.percentile(0.99);
  r.p999 = h.percentile(0.999);
  r.pmax = h.percentile(1.0);
  return r;
}

void print_row(const Result& r) {
  std::printf("%-38s %12.0f %10llu %10llu %10llu %10llu\n", r.name.c_str(),
              r.throughput, (unsigned long long)r.p50, (unsigned long long)r.p99,
              (unsigned long long)r.p999, (unsigned long long)r.pmax);
}

}  // namespace

void print_table(const char* title, const Result& base, const Result& tf) {
  std::printf("\n=== %s ===\n", title);
  std::printf("%-38s %12s %10s %10s %10s %10s\n", "config", "orders/sec",
              "p50(ns)", "p99(ns)", "p999(ns)", "max(ns)");
  print_row(base);
  print_row(tf);
  if (base.p99 > 0) {
    // Signed ratio so a regression (tf > base) shows as a negative number
    // instead of underflowing unsigned subtraction.
    const double red = 100.0 * (1.0 - (double)tf.p99 / (double)base.p99);
    std::printf("-> P99 latency reduction (TradeFlow vs baseline): %.1f%%\n", red);
  }
  std::printf("-> Throughput speedup: %.2fx\n", tf.throughput / base.throughput);
}

int main(int argc, char** argv) {
  WorkloadConfig cfg;
  std::size_t ring_cap = 1 << 16;
  unsigned producer_threads = 4;
  double rate = 1'000'000.0;  // sustained aggregate rate for the latency run
  std::string csv_path = "bench/results/latest.csv";
  for (int i = 1; i < argc - 1; ++i) {
    if (!std::strcmp(argv[i], "--symbols")) cfg.num_symbols = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--orders")) cfg.num_orders = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--producers")) producer_threads = std::stoul(argv[++i]);
    else if (!std::strcmp(argv[i], "--ring")) ring_cap = std::stoull(argv[++i]);
    else if (!std::strcmp(argv[i], "--rate")) rate = std::stod(argv[++i]);
    else if (!std::strcmp(argv[i], "--csv")) csv_path = argv[++i];
  }

  std::printf("Generating %llu orders across %u symbols (seed %u)...\n",
              (unsigned long long)cfg.num_orders, cfg.num_symbols, cfg.seed);
  const auto orders = bench::generate(cfg);

  // Phase 1: saturation (no pacing) - measures MAX sustainable throughput.
  std::printf("\n[1/2] Saturation run (max offered load)...\n");
  const Result base_sat = run_baseline(orders, cfg.num_symbols, producer_threads, 0.0);
  const Result tf_sat = run_tradeflow(orders, cfg.num_symbols, ring_cap, producer_threads, 0.0);
  print_table("Throughput @ saturation", base_sat, tf_sat);

  // Phase 2: paced injection - measures tail latency at a sustained rate that
  // both engines can keep up with (defensible "latency @ rate" comparison).
  Result base_lat = base_sat, tf_lat = tf_sat;
  if (rate > 0.0) {
    std::printf("\n[2/2] Paced run (%.0f orders/sec sustained)...\n", rate);
    base_lat = run_baseline(orders, cfg.num_symbols, producer_threads, rate);
    tf_lat = run_tradeflow(orders, cfg.num_symbols, ring_cap, producer_threads, rate);
    char title[96];
    std::snprintf(title, sizeof(title), "Latency @ %.0f orders/sec sustained", rate);
    print_table(title, base_lat, tf_lat);
  }

  std::ofstream csv(csv_path);
  if (csv) {
    csv << "phase,config,throughput,p50,p99,p999,max,processed\n";
    auto row = [&](const char* phase, const Result& r) {
      csv << phase << ',' << r.name << ',' << r.throughput << ',' << r.p50 << ','
          << r.p99 << ',' << r.p999 << ',' << r.pmax << ',' << r.processed << '\n';
    };
    row("saturation", base_sat);
    row("saturation", tf_sat);
    if (rate > 0.0) {
      row("paced", base_lat);
      row("paced", tf_lat);
    }
    std::printf("\nWrote %s\n", csv_path.c_str());
  }
  return 0;
}
