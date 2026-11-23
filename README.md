# silica-allah-sim (C++20)

A zero-dependency, ISA-level AArch64 **functional** simulator core. Header-only core in `include/silica/`.
Designed for decode throughput and architectural correctness, not cycle accuracy.

> Performance figures in the original spec documents are design targets, not measured results.
> Only the table below is measured.

## Build

    make test      # unit tests (164 checks)
    make san       # same tests under ASan + UBSan
    make bench     # micro-benchmarks
    ./build/silica-sim [--max N] [prog.bin]   # runs a built-in demo (sum 1..10) if no file

Requires GCC 10+/Clang 10+ (C++20), little-endian host. Flags: `-O3 -flto -fno-exceptions -fno-rtti`.
`make NATIVE=1` adds `-march=native` (made no measurable difference here).

## Layout

| File | Role |
|---|---|
| `registers.hpp` | X0-X30/XZR/SP, V0-V31, PC, PSTATE, fixed-index sysreg slots. No heap. |
| `memory.hpp` | Flat little-endian memory; out-of-range sets a sticky fault flag. |
| `decoder.hpp` | Hand-written decode tree; `decode_into(word, Instruction&)`, POD output. |
| `alu.hpp` | AddWithCarry, shifts, ConditionHolds. |
| `semantics.hpp` | Per-group semantics (`dp_imm`, `dp_reg`, `branch`, `ldst`, `system`). |
| `cpu.hpp` | `Cpu::step()` / `run()`. No exceptions, no `Result`; check `halt`. |
| `asm.hpp` | Test/bench-only encoders. |

## Instruction coverage

ADD/SUB imm and shifted-reg (incl. ADDS/SUBS, CMP/CMN), ADC/SBC (+S), AND/ORR/EOR imm and shifted-reg (BIC/ORN/EON, ANDS/TST),
MOVZ/MOVN/MOVK, ADR/ADRP, LSLV/LSRV/ASRV/RORV, MADD/MSUB, UDIV/SDIV, CSEL/CSINC/CSINV/CSNEG,
B/BL/BR/BLR/RET, B.cond, CBZ/CBNZ, TBZ/TBNZ, LDR/STR (B/H/W/X, sign-extending forms, unsigned-offset/unscaled/pre/post),
LDP/STP (W/X; offset/pre/post), MRS/MSR (NZCV, TPIDR_EL0, VBAR_EL1, ELR_EL1, SPSR_EL1), NOP/hints, DMB/DSB/ISB (no-ops).

**Not yet implemented** (decode to `Invalid`, CPU halts): extended-register add/sub, CCMP/CCMN, shifts-by-immediate aliases
that use bitfield moves (UBFM/SBFM/EXTR), LDPSW/LDNP/STNP, register-offset loads/stores, LSE atomics, pointer auth,
SVC/ERET/exception model, SIMD/FP/SVE/SME. Alignment checks are not modelled. See `docs/limitations.md`.

## Measured results

Host: Intel Xeon @ 2.10 GHz (shared cloud VM, 1 vCPU, no core pinning possible), GCC 13.3.0, `-O3 -flto`.
Best of 7 runs. Treat as indicative only; the VM is noisy.

| Benchmark | Spec target | Measured |
|---|---|---|
| decode (varied mix) | 500 M insn/s | ~285 M insn/s |
| execute ALU+branch loop | 1.0 G ops/s | ~170 M insn/s |
| execute mixed (2 ld/st, 4 DP, 1 br) | 300 M insn/s | ~130 M insn/s |

The targets are **not met**. Likely levers (untested): cache decoded instructions per PC, fuse CMP/SUBS+B.cond,
computed-goto/threaded dispatch, avoid re-initialising the `Instruction` each step.
No Rust build was available here, so there is **no C++-vs-Rust comparison yet**.

## Judgement calls (Arm ARM)

- Barriers (DMB/DSB/ISB) and hints are no-ops in a functional model.
- Divide-by-zero returns 0 and `INT_MIN / -1` wraps, per the Arm ARM (no trap).
- Unmodelled system registers decode to `Invalid` rather than being invented.
- Unaligned accesses succeed (alignment faults depend on SCTLR, not modelled).
