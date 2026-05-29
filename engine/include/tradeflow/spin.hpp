#pragma once

namespace tradeflow {

// CPU "pause"/"yield" hint for busy-wait loops. Reduces power draw and frees
// the SMT sibling/core while spinning, without adding latency to real work
// (it is only called when there is nothing to process).
inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  asm volatile("yield" ::: "memory");
#else
  // portable fallback: no-op
#endif
}

}  // namespace tradeflow
