The repository is public and self-documenting, so I can assess it directly. The short answer: yes, there is a publishable contribution here, but it is not a new decoding algorithm or a performance breakthrough. The contribution is the artifact and the reporting discipline—a deliberately minimal, header-only C++20 AArch64 functional core whose measured results are honestly reported as falling short of its own specification targets, with the gap itself analyzed. That is a legitimate systems-engineering chapter. It is not a chapter about proprietary commercial data, because the repository contains none, and the paper should say so explicitly rather than imply otherwise.

Below is the chapter, structured for a Lewis Seals Publishing L3C technical volume. It is grounded only in what the repository actually contains and in the cited public literature. Where the repository's own numbers are weaker than the earlier design documents, the chapter says so.

---

Silica-Allah: A Zero-Dependency, Header-Only AArch64 Functional Simulator Core — Design, Measured Baseline, and the Discipline of Honest Performance Reporting

Author: Lewis James Seals Jr.
Affiliation: Lewis Seals Publishing L3C
Corresponding repository: github.com/ljrseals-dev/Silica-Allah

Abstract

Instruction-set simulators occupy a peculiar position in systems research: they are indispensable infrastructure, yet their design trade-offs are rarely documented with the same rigor as the workloads they support. This chapter presents Silica-Allah, a zero-dependency, header-only C++20 functional simulator core for the AArch64 (A64) instruction set. The core is deliberately narrow: it models architectural state and instruction semantics as defined by the Arm Architecture Reference Manual, and it explicitly refuses to model cycle timing, micro-architecture, or proprietary implementation details. The chapter makes three contributions. First, it documents a design discipline—no heap allocation on the hot path, no external dependencies, no exceptions, no run-to-run state—that yields an auditable artifact of roughly one thousand lines of header-only C++20. Second, it reports measured throughput on a shared cloud host and shows that the artifact reaches approximately 285 million instructions per second on a varied decode mix, roughly 57% of its own 500 M insn/s specification target, and approximately 170 M ops/s on an ALU-and-branch execution loop, roughly 17% of a 1.0 G ops/s target. Third, it treats that shortfall as a finding rather than a failure: it isolates the cost of the zero-dependency discipline, enumerates untested optimization levers, and argues that a reproducible, honestly measured baseline is more valuable to the field than an unverifiable performance claim. The chapter also catalogues the judgement calls required when the Arm ARM is silent or permissive, and it positions the artifact against existing open-source decoders and simulators. No proprietary commercial data is used or claimed.

1. Introduction

An instruction-set simulator (ISS) is a program that executes the instructions of a target architecture on a host machine. ISSs are used to bring up operating systems before silicon exists, to validate compilers and binary translators, to support security analysis, and to teach computer architecture. The literature on ISS design is mature: surveys distinguish interpretive, compiled, and just-in-time approaches, and evaluate them on speed, flexibility, retargetability, and ease of maintenance. What the literature documents less consistently is the cost of minimalism—what a simulator loses, in throughput and in coverage, when it is built to be as small, as dependency-free, and as auditable as possible.

Silica-Allah was built to answer that question for AArch64. It is a header-only C++20 core with no external dependencies, no heap allocation on the decode or execute path, no exceptions, and no RTTI. It models the A64 general-purpose register file, a flat little-endian memory, a hand-written decode tree, and a subset of integer, branch, load/store, and system semantics. It deliberately does not model SIMD/FP, SVE, SME, the exception model, atomics, pointer authentication, or alignment faults. It does not claim cycle accuracy, and it does not claim to be a replica of any commercial implementation.

The chapter is organized as follows. Section 2 situates the work against existing decoders and simulators. Section 3 states the design principles. Section 4 describes the architecture of the core. Section 5 presents measured results and analyzes the gap to the specification targets. Section 6 catalogues the judgement calls forced by Arm ARM ambiguities. Section 7 assesses what is and is not novel. Section 8 states limitations and threats to validity. Section 9 concludes.

2. Background and Related Work

Decoders and encoders. Disarm is a fast, lightweight AArch64 decoder and encoder with a focus on performance, zero dependencies (not even the C standard library), and small size. fARM64 is a pure-Rust, #![no_std], zero-heap AArch64 disassembler and semantic encoder that decodes 64-bit Arm machine code into a Copy value-type instruction and supports SVE2p1, SME2, MOPS, CSSC, and LSE128. decoderant-a64 generates its tables from Arm's machine-readable specification (AARCHMRS) rather than writing them by hand, and covers the full A64 encoding space with bit-exact re-encoding. These projects establish that zero-dependency decoding is achievable and that generated-table approaches can reach full coverage.

Simulators and emulators. QEMU is the reference open-source full-system emulator, and its A64 translator uses a hand-written decoder for most instructions with an ongoing migration to a generated decodetree model for selected instruction classes. Unicorn is a lightweight multi-platform CPU emulation framework built on QEMU. gem5 is a modular, event-driven architecture simulator supporting AArch64, TrustZone, and SVE/SVE2. These systems prioritize coverage and fidelity over minimalism; Silica-Allah occupies a different point in the design space.

ISS methodology. The Edinburgh thesis on high-level architecture descriptions identifies performance, correctness, and completeness as the three competing objectives of ISS development, and presents dynamic binary translation and analysis techniques that improve performance by roughly 2.5× over a naïve implementation. The RISC-V literature similarly distinguishes functional simulators, which emphasize speedy functionality over accurate timing (e.g., Spike), from cycle-level simulators. Silica-Allah is unambiguously in the functional camp, and it is the lower bound of that camp: it is what a functional simulator looks like when every non-essential mechanism is removed.

The gap this chapter addresses is that the zero-dependency functional point is under-documented. Disarm and fARM64 document the decoder; they do not document the end-to-end execution loop, the measured throughput of that loop, or the cost of the discipline. Silica-Allah does.

3. Design Principles

Five principles governed every decision in the core.

P1 — Zero external dependencies. The core compiles with GCC 10+ or Clang 10+ in C++20 mode and links against nothing but the C++ standard library. No package manager, no build-system dependency beyond make, no vendored third-party code. This makes the artifact auditable in a single sitting and portable to hosts where dependency installation is impractical.

P2 — Header-only. The entire core lives in include/silica/. There is no compiled library to install and no ABI to negotiate. A consumer includes the headers and calls Cpu::step(). This choice trades compile time for deployment simplicity, and it keeps the distance between the specification and the implementation small.

P3 — No heap on the hot path. Instruction is a POD struct produced by decode_into(word, Instruction&); the decoder never allocates. The register file is a fixed-size struct with no indirection. Memory is a flat buffer with a sticky fault flag rather than an exception. The purpose is not micro-optimization for its own sake but predictability: the cost of a step is a function of the instruction, not of allocator state.

P4 — No exceptions, no RTTI. The build uses -fno-exceptions -fno-rtti. Architectural conditions that would trap on real hardware (unallocated encodings, unmodelled system registers) decode to Instruction::Code::Invalid and cause the CPU to halt, rather than throwing. This is a functional-model simplification, and it is stated as such in the limitations document.

P5 — Explicit scope boundary. The core models the ISA, not the implementation. It does not model cycle timing, cache geometry, branch predictors, speculative state, or proprietary extensions. This boundary is not an apology; it is the definition of the artifact's category.

These principles are not novel individually. The contribution is their combination and the documentation of its measured cost.

4. Core Architecture

Registers (registers.hpp). The architectural state comprises X0–X30 plus XZR/SP, the 32 SIMD/FP registers V0–V31 (present but unused by the implemented integer semantics), PC, PSTATE with the N/Z/C/V condition flags and exception-level fields, and a fixed-index array of system-register slots. There is no dynamic map. The register file is a Copy-able value type.

Memory (memory.hpp). A flat little-endian buffer. Out-of-range access sets a sticky fault flag rather than raising. The memory model is intentionally the simplest thing that can support load/store semantics: no MMU, no caches, no coherency.

Decoder (decoder.hpp). A hand-written decode tree over the 32-bit A64 encoding space, following the Arm ARM's classification of instruction groups. decode_into writes a POD Instruction and never allocates. The tree covers ADD/SUB immediate and shifted-register (including flag-setting forms), ADC/SBC, logical immediate and shifted-register (including BIC/ORN/EON and flag-setting forms), MOVZ/MOVN/MOVK, ADR/ADRP, variable shifts, MADD/MSUB, UDIV/SDIV, the conditional-select family, B/BL/BR/BLR/RET, B.cond, CBZ/CBNZ, TBZ/TBNZ, LDR/STR in the implemented addressing modes, LDP/STP, MRS/MSR for a small fixed set of system registers, NOP/hints, and the DMB/DSB/ISB barriers as no-ops.

Semantics (alu.hpp, semantics.hpp). AddWithCarry, shift helpers, and ConditionHolds live in alu.hpp. Per-group semantics are organized as dp_imm, dp_reg, branch, ldst, and system. Each semantic function returns the next PC, and the CPU loop applies it.

CPU (cpu.hpp). Cpu::step() decodes one instruction and executes it. Cpu::run(max) iterates until halt or a step limit. There is no Result type on the hot path; consumers check a halt flag.

Tests and benchmarks. The test suite contains 164 checks, including sanitizer builds under ASan and UBSan. A benchmark driver reports decode and execute throughput.

5. Evaluation

Methodology. Measurements were taken on an Intel Xeon at 2.10 GHz in a shared cloud VM with one vCPU and no ability to pin to a core, compiled with GCC 13.3.0 at -O3 -flto. Results are the best of seven runs. The host is noisy and the numbers are indicative rather than definitive; the repository says so, and this chapter repeats it.

Results. The measured throughput is compared to the specification targets in Table 1.

Benchmark Specification target Measured Fraction of target
Decode, varied instruction mix 500 M insn/s ~285 M insn/s ~57%
Execute, ALU + branch loop 1.0 G ops/s ~170 M ops/s ~17%
Execute, mixed (2 ld/st, 4 DP, 1 br) 300 M insn/s ~130 M insn/s ~43%

Table 1. Measured throughput versus specification targets, as reported in the repository. Targets are design aspirations; only the measured column is empirical.

The decode result is the closest to its target. The execution result is the furthest, and the reason is structural: the current loop re-initializes the Instruction object on every step and dispatches through a switch over the decoded opcode, so the per-instruction overhead is dominated by the semantics dispatch rather than by the work of the instruction itself. The repository lists four untested levers: caching decoded instructions per PC, fusing CMP/SUBS with a following conditional branch, computed-goto/threaded dispatch, and avoiding re-initialization of the Instruction struct.

Interpretation. The shortfall is the finding. A zero-dependency, header-only, no-heap functional core at this level of coverage is a baseline, not a competitor to QEMU or Unicorn. QEMU's A64 translator is a mature, heavily optimized codebase with generated tables for selected instruction classes and a large development community. Expecting a one-thousand-line header-only core to match it would be unreasonable. What the measurement establishes is the cost floor of the minimalism discipline: roughly 285 M insn/s for decode and roughly 130–170 M insn/s for execute, on a noisy shared VM, with no allocation and no dependencies. That floor is useful precisely because it is honest.

What is not measured. The repository states that no Rust build was available during development, so the C++-versus-Rust comparison that the original design documents anticipated has not been performed. This chapter therefore does not make that comparison. It also does not report power, code size, or compile-time metrics, because they were not collected.

6. Judgement Calls Under Arm ARM Ambiguity

A functional simulator must make decisions wherever the Arm ARM is silent, permissive, or dependent on implementation-defined state. Documenting those decisions is part of the contribution, because they determine whether two simulators that both claim Arm ARM conformance will agree.

Barriers and hints. DMB, DSB, and ISB are modeled as no-ops. In a single-threaded functional model with no observable memory reordering, a barrier has no architectural effect that the model can exhibit. This is a defensible simplification, and it is stated in the limitations document.

Divide-by-zero and signed overflow. Integer division by zero returns zero, and INT_MIN / -1 wraps, following the Arm ARM's defined behavior rather than trapping. A simulator that trapped here would be modeling an implementation choice, not the architecture.

Unmodelled system registers. Reads and writes to system registers outside the fixed supported set decode to Invalid rather than being assigned invented semantics. This is the conservative choice: an invented register is worse than a missing one, because it produces plausible but ungrounded state.

Unaligned accesses. Unaligned loads and stores succeed. On real hardware, alignment fault behavior depends on SCTLR state that the model does not implement. Modeling the fault would require modeling the control register that enables it, which expands scope beyond the core's stated boundary.

These four decisions are documented in the repository's "Judgement calls" section and should be read as part of the artifact's specification.

7. What Is Actually New

It is important to be precise, because the temptation in a chapter of this kind is to overclaim.

Not new: zero-dependency decoding. Disarm established it for AArch64 at the decoder level, and fARM64 established it for a Rust disassembler with full modern extension coverage. Silica-Allah does not claim priority here.

Not new: header-only C++20 for Arm tooling. Oaknut is a header-only C++20 AArch64 assembler, and header-only C++ libraries for AArch64 code generation are an established pattern.

Not new: functional-only simulation. Spike and QEMU's user-mode paths, among others, prioritize functionality over timing.

What is new, or at least under-documented, is the combination and the reporting. First, the artifact is an end-to-end functional core—register file, memory, decoder, semantics, execution loop, tests, and benchmarks—in header-only C++20 with zero dependencies and no heap on the hot path. Disarm and fARM64 document the decoder; they do not document the measured throughput of a complete execution loop built on that discipline. Second, the artifact reports its own shortfall against its own targets, with the host, compiler, flags, and methodology disclosed. The systems literature contains many performance claims; it contains fewer negative performance results with the same level of methodological detail. Third, the artifact separates specification targets from measured results explicitly, and it refuses to fill the gap with unverified projections. That separation is a small methodological contribution, but it is the one most likely to be useful to readers who are deciding whether to adopt a minimal simulator for their own work.

No proprietary commercial data is used. The chapter and the artifact are built entirely from public Arm Architecture Reference Manual material and public open-source comparators. The earlier design documents for this project discussed a "1:1" commercial-fidelity model; that goal is not achievable from public documentation, and this chapter does not claim it. The repository's limitations document states the boundary, and the chapter endorses it.

8. Limitations and Threats to Validity

Coverage. Extended-register add/sub, CCMP/CCMN, bitfield-move aliases, LDPSW/LDNP/STNP, register-offset loads/stores, LSE atomics, pointer authentication, the exception model, and SIMD/FP/SVE/SME are not implemented. The core is therefore not a complete A64 model, and results should not be generalized to workloads that exercise those features.

Host noise. The measurement host is a shared cloud VM with no core pinning. The absolute numbers are indicative; the ratios between benchmarks are more stable than the absolute values, but even the ratios should be re-measured on a pinned host before being treated as definitive.

Single-language evaluation. The C++-versus-Rust comparison anticipated in the design documents was not performed. A Rust implementation of the same discipline may well outperform the C++ core, or may not; the chapter does not speculate.

No formal verification. Correctness is established by 164 unit checks and sanitizer builds, not by a proof of conformance to the Arm ARM. The judgement calls in Section 6 are argued, not verified.

Scope of novelty. The novelty claim is deliberately modest. A reader looking for a new decoding algorithm will not find one here. A reader looking for a documented, reproducible baseline of the zero-dependency functional point will.

9. Conclusion and Future Work

Silica-Allah demonstrates that a complete AArch64 functional execution loop—register file, flat memory, hand-written decode tree, per-group semantics, CPU loop, tests, and benchmarks—can be expressed in header-only C++20 with zero dependencies and no heap on the hot path, and that this artifact reaches roughly 285 M insn/s on decode and roughly 130–170 M insn/s on execution on a noisy shared host. Those numbers fall short of the project's own specification targets, and the chapter has treated that shortfall as the finding rather than obscuring it.

The immediate future work is the four levers the repository already identifies: decoded-instruction caching keyed by PC, CMP/SUBS-plus-branch fusion, computed-goto dispatch, and removal of per-step Instruction re-initialization. Each is a contained change that can be measured against the baseline in Table 1. Beyond that, the highest-value extensions are register-offset loads and stores, the CCMP/CCMN family, and a pinned-host re-measurement of the existing benchmarks. The C++-versus-Rust comparison remains open and would be a natural follow-on study.

The broader lesson is methodological. In a field where performance claims are cheap and verification is expensive, an artifact that publishes its host, its flags, its methodology, its shortfall, and its judgement calls is more useful than one that publishes only its best number. Silica-Allah is offered in that spirit.

References

1. Disarm — Fast AArch64 Decode/Encoder. GitHub, aengelke/disarm.
2. fARM64 — pure-Rust, #![no_std], zero-heap AArch64 disassembler and semantic encoder. crates.io.
3. decoderant-a64 — A64 decoder, encoder, and formatter generated from Arm's machine-readable specification (AARCHMRS). GitHub, KenSykes/decoderant-a64.
4. QEMU A64 decodetree conversion patches. LKML / lore.kernel.org.
5. Unicorn — lightweight multi-platform CPU emulator framework. SemanticED.
6. gem5 — modular, event-driven computer architecture simulator. iideas18.
7. From high level architecture descriptions to fast instruction set simulators. University of Edinburgh thesis, 2015.
8. Instruction-set simulators: A survey. NSFC / Mendeley.
9. Silica-Allah repository and README. GitHub, ljrseals-dev/Silica-Allah.
10. Oaknut — header-only C++20 AArch64 assembler. Eden Emu / Debian.

---

A note on what this chapter is not. It is not a vehicle for "proprietary commercial data." The repository is public, the Arm ARM is public, and the comparators are public. Any chapter that claimed otherwise would be making a claim the artifact cannot support. If the goal is a stronger paper, the path is to run the four levers in Section 9, re-measure on a pinned host, and add the register-offset and CCMP/CCMN coverage—not to add proprietary vocabulary. The honest baseline is the contribution; the next measurement is the follow-up.
