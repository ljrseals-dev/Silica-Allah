# Limitations and Non-Goals

This is an **architectural (ISA-level) model** of publicly documented A64 behaviour. It is not:
- cycle-accurate or micro-architectural (no pipeline, latencies, caches, predictors);
- a model of any commercial core (no Neural Engine, Secure Enclave, or undocumented registers);
- a system emulator (no MMU, devices, interrupts, exception levels beyond PSTATE fields).

That data is not publicly specified, so no "1:1" or silicon-accurate claim is made. The honest claims are
decode throughput and architectural correctness over the implemented subset.
