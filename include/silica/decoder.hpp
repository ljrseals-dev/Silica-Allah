#pragma once
// Hand-written A64 decode tree (Arm ARM, "A64 instruction set encoding").
// decode_into() writes into a caller-owned POD Instruction: no heap, no
// exceptions, no Result. Anything unrecognised -> Code::Invalid.
#include "common.hpp"
#include "registers.hpp"  // SysIdx for the MRS/MSR map

namespace silica {

enum class Code : uint8_t {
  Invalid, Halt, Nop, Brk,
  // data-processing (immediate)
  AddImm, SubImm, MovZ, MovN, MovK, AndImm, OrrImm, EorImm, Adr, Adrp,
  // data-processing (register)
  AddReg, SubReg, AndReg, OrrReg, EorReg, Adc, Sbc,
  Lslv, Lsrv, Asrv, Rorv, Madd, Msub, Udiv, Sdiv,
  Csel, Csinc, Csinv, Csneg,
  // branches
  B, Bl, Br, Blr, Ret, BCond, Cbz, Cbnz, Tbz, Tbnz,
  // loads / stores (GPR)
  Ldr, Str, Ldp, Stp,
  // system
  Mrs, Msr,
  Count
};

// Field conventions (documented here, used by semantics):
//  rd  : destination; Rt for CBZ/TBZ/LDR/STR/LDP/STP/MRS/MSR
//  rn  : first source / base register; rm second source; ra addend / Rt2 (LDP/STP)
//  sf  : 1 = 64-bit operation
//  imm : decoded immediate (byte offset for branches/ldst, bitmask for logical imm)
//  amount : shift amount / MOVK shift / TBZ bit number
//  size: log2 access size for single-register LDR/STR
//  mode: 0 offset/unscaled, 1 pre-index, 2 post-index
struct Instruction {
  uint32_t word = 0;
  Code code = Code::Invalid;
  uint8_t rd = 0, rn = 0, rm = 0, ra = 0;
  uint8_t sf = 1;
  uint8_t setflags = 0;
  uint8_t inv = 0;
  uint8_t shift = 0;
  uint8_t amount = 0;
  uint8_t cond = 0;
  uint8_t size = 0;
  uint8_t sext = 0;
  uint8_t mode = 0;
  int64_t imm = 0;
};
static_assert(sizeof(Instruction) <= 112, "Instruction must stay small and Copy-friendly");

// DecodeBitMasks (Arm ARM shared pseudocode) for logical immediates.
constexpr bool decode_bit_masks(uint32_t N, uint32_t imms, uint32_t immr,
                                unsigned datasize, uint64_t& wmask) noexcept {
  const uint32_t v = (N << 6) | (~imms & 0x3Fu);
  if (v == 0) return false;
  const unsigned len = 31u - static_cast<unsigned>(std::countl_zero(v));
  if (len < 1) return false;
  const unsigned esize = 1u << len;
  if (esize > datasize) return false;
  const uint32_t levels = esize - 1;
  const uint32_t S = imms & levels;
  const uint32_t R = immr & levels;
  if (S == levels) return false;  // logical immediate: all-ones element is reserved
  const uint64_t emask = esize == 64 ? ~0ull : ((1ull << esize) - 1);
  const uint64_t welem = (1ull << (S + 1)) - 1;  // S+1 <= esize-1 < 64
  uint64_t rot = R == 0 ? welem : (((welem >> R) | (welem << (esize - R))) & emask);
  for (unsigned e = esize; e < datasize; e <<= 1) rot |= rot << e;
  wmask = rot;
  return true;
}

namespace detail {

inline void decode_dp_imm(uint32_t w, Instruction& i) noexcept {
  i.rd = static_cast<uint8_t>(bits(w, 4, 0));
  i.rn = static_cast<uint8_t>(bits(w, 9, 5));
  i.sf = static_cast<uint8_t>(bits(w, 31, 31));
  switch (bits(w, 25, 23)) {
    case 0b000: case 0b001: {  // PC-relative addressing: ADR / ADRP
      const int64_t imm = sext((static_cast<uint64_t>(bits(w, 23, 5)) << 2) | bits(w, 30, 29), 21);
      if (bits(w, 31, 31)) { i.code = Code::Adrp; i.imm = imm * 4096; }
      else { i.code = Code::Adr; i.imm = imm; }
      i.sf = 1;
      return;
    }
    case 0b010:  // ADD/SUB (immediate)
      i.code = bits(w, 30, 30) ? Code::SubImm : Code::AddImm;
      i.setflags = static_cast<uint8_t>(bits(w, 29, 29));
      i.imm = static_cast<int64_t>(bits(w, 21, 10)) << (bits(w, 22, 22) ? 12 : 0);
      return;
    case 0b100: {  // logical (immediate)
      uint64_t mask;
      if (!decode_bit_masks(bits(w, 22, 22), bits(w, 15, 10), bits(w, 21, 16),
                            i.sf ? 64 : 32, mask)) return;
      const uint32_t opc = bits(w, 30, 29);
      i.code = opc == 0 || opc == 3 ? Code::AndImm : opc == 1 ? Code::OrrImm : Code::EorImm;
      i.setflags = opc == 3;
      i.imm = static_cast<int64_t>(mask);
      return;
    }
    case 0b101: {  // move wide
      const uint32_t opc = bits(w, 30, 29), hw = bits(w, 22, 21);
      if (opc == 1 || (!i.sf && hw > 1)) return;
      const uint64_t imm16 = bits(w, 20, 5);
      const unsigned sh = hw * 16;
      if (opc == 0) {
        uint64_t val = ~(imm16 << sh);
        if (!i.sf) val &= 0xFFFFFFFFull;
        i.code = Code::MovN; i.imm = static_cast<int64_t>(val);
      } else if (opc == 2) {
        i.code = Code::MovZ; i.imm = static_cast<int64_t>(imm16 << sh);
      } else {
        i.code = Code::MovK; i.imm = static_cast<int64_t>(imm16); i.amount = static_cast<uint8_t>(sh);
      }
      return;
    }
    default: return;
  }
}

inline void decode_branch_sys(uint32_t w, Instruction& i) noexcept {
  i.rd = static_cast<uint8_t>(bits(w, 4, 0));
  i.rn = static_cast<uint8_t>(bits(w, 9, 5));
  if ((w & 0x7C000000u) == 0x14000000u) {  // B / BL
    i.code = bits(w, 31, 31) ? Code::Bl : Code::B;
    i.imm = sext(bits(w, 25, 0), 26) * 4;
  } else if ((w & 0xFF000010u) == 0x54000000u) {  // B.cond
    i.code = Code::BCond; i.cond = static_cast<uint8_t>(bits(w, 3, 0));
    i.imm = sext(bits(w, 23, 5), 19) * 4;
  } else if ((w & 0x7E000000u) == 0x34000000u) {  // CBZ / CBNZ
    i.code = bits(w, 24, 24) ? Code::Cbnz : Code::Cbz;
    i.sf = static_cast<uint8_t>(bits(w, 31, 31));
    i.imm = sext(bits(w, 23, 5), 19) * 4;
  } else if ((w & 0x7E000000u) == 0x36000000u) {  // TBZ / TBNZ
    i.code = bits(w, 24, 24) ? Code::Tbnz : Code::Tbz;
    i.amount = static_cast<uint8_t>((bits(w, 31, 31) << 5) | bits(w, 23, 19));
    i.imm = sext(bits(w, 18, 5), 14) * 4;
  } else if ((w & 0xFFFFFC1Fu) == 0xD61F0000u) { i.code = Code::Br;
  } else if ((w & 0xFFFFFC1Fu) == 0xD63F0000u) { i.code = Code::Blr;
  } else if ((w & 0xFFFFFC1Fu) == 0xD65F0000u) { i.code = Code::Ret;
  } else if ((w & 0xFFE0001Fu) == 0xD4200000u) { i.code = Code::Brk; i.imm = bits(w, 20, 5);
  } else if ((w & 0xFFFFF01Fu) == 0xD503201Fu) { i.code = Code::Nop;   // HINT
  } else if ((w & 0xFFFFF09Fu) == 0xD503309Fu) { i.code = Code::Nop;   // DSB/DMB/ISB: ordering no-op
  } else if ((w & 0xFFF00000u) == 0xD5300000u || (w & 0xFFF00000u) == 0xD5100000u) {  // MRS / MSR (register)
    int idx = -1;
    switch (bits(w, 19, 5)) {  // o0:op1:CRn:CRm:op2
      case 0x5A10: idx = SYS_NZCV; break;
      case 0x5E82: idx = SYS_TPIDR_EL0; break;
      case 0x4600: idx = SYS_VBAR_EL1; break;
      case 0x4201: idx = SYS_ELR_EL1; break;
      case 0x4200: idx = SYS_SPSR_EL1; break;
      default: break;
    }
    if (idx < 0) return;  // unmodelled system register -> Invalid
    i.code = (w & 0x00200000u) ? Code::Mrs : Code::Msr;
    i.imm = idx;
  }
}

inline void decode_ldst(uint32_t w, Instruction& i) noexcept {
  i.rd = static_cast<uint8_t>(bits(w, 4, 0));
  i.rn = static_cast<uint8_t>(bits(w, 9, 5));
  if ((w & 0x3E000000u) == 0x28000000u) {  // LDP/STP (GPR)
    const uint32_t opc = bits(w, 31, 30), m = bits(w, 24, 23);
    if ((opc != 0 && opc != 2) || m == 0) return;  // LDPSW / STNP / LDNP not modelled
    i.sf = opc == 2;
    i.code = bits(w, 22, 22) ? Code::Ldp : Code::Stp;
    i.ra = static_cast<uint8_t>(bits(w, 14, 10));
    i.mode = m == 2 ? 0 : m == 3 ? 1 : 2;
    i.imm = sext(bits(w, 21, 15), 7) * (i.sf ? 8 : 4);
  } else if ((w & 0x3E000000u) == 0x38000000u) {  // LDR/STR/LDUR/STUR (GPR)
    const uint32_t size = bits(w, 31, 30), opc = bits(w, 23, 22);
    if (opc == 3 && size >= 2) return;                    // reserved
    if (bits(w, 25, 24) == 1) {                           // unsigned offset
      i.mode = 0; i.imm = static_cast<int64_t>(bits(w, 21, 10)) << size;
    } else if (bits(w, 25, 24) == 0 && bits(w, 21, 21) == 0) {  // imm9 forms
      const uint32_t m = bits(w, 11, 10);
      if (m == 2) return;                                 // LDTR/STTR: not modelled
      i.mode = m == 0 ? 0 : m == 1 ? 2 : 1;               // unscaled / post / pre
      i.imm = sext(bits(w, 20, 12), 9);
    } else return;
    if (opc == 2 && size == 3) { i.code = Code::Nop; return; }  // PRFM/PRFUM: hint only
    i.size = static_cast<uint8_t>(size);
    i.code = opc == 0 ? Code::Str : Code::Ldr;
    i.sext = opc >= 2;
    i.sf = opc == 2 ? 1 : opc == 3 ? 0 : (size == 3);
  }
}

inline void decode_dp_reg(uint32_t w, Instruction& i) noexcept {
  i.rd = static_cast<uint8_t>(bits(w, 4, 0));
  i.rn = static_cast<uint8_t>(bits(w, 9, 5));
  i.rm = static_cast<uint8_t>(bits(w, 20, 16));
  i.sf = static_cast<uint8_t>(bits(w, 31, 31));
  switch (bits(w, 28, 24)) {
    case 0b01010: {  // logical (shifted register)
      if (!i.sf && bits(w, 15, 15)) return;
      const uint32_t opc = bits(w, 30, 29);
      i.code = opc == 0 || opc == 3 ? Code::AndReg : opc == 1 ? Code::OrrReg : Code::EorReg;
      i.setflags = opc == 3;
      i.shift = static_cast<uint8_t>(bits(w, 23, 22));
      i.amount = static_cast<uint8_t>(bits(w, 15, 10));
      i.inv = static_cast<uint8_t>(bits(w, 21, 21));
      return;
    }
    case 0b01011:  // add/sub (shifted register); extended-register form not modelled
      if (bits(w, 21, 21) || bits(w, 23, 22) == 3 || (!i.sf && bits(w, 15, 15))) return;
      i.code = bits(w, 30, 30) ? Code::SubReg : Code::AddReg;
      i.setflags = static_cast<uint8_t>(bits(w, 29, 29));
      i.shift = static_cast<uint8_t>(bits(w, 23, 22));
      i.amount = static_cast<uint8_t>(bits(w, 15, 10));
      return;
    case 0b11010:
      switch (bits(w, 23, 21)) {
        case 0b000:  // ADC / SBC
          if (bits(w, 15, 10) != 0) return;
          i.code = bits(w, 30, 30) ? Code::Sbc : Code::Adc;
          i.setflags = static_cast<uint8_t>(bits(w, 29, 29));
          return;
        case 0b100: {  // conditional select
          if (bits(w, 29, 29) || bits(w, 11, 11)) return;
          const uint32_t op = bits(w, 30, 30), o2 = bits(w, 10, 10);
          i.code = op == 0 ? (o2 ? Code::Csinc : Code::Csel) : (o2 ? Code::Csneg : Code::Csinv);
          i.cond = static_cast<uint8_t>(bits(w, 15, 12));
          return;
        }
        case 0b110:  // data-processing (2 source)
          if (bits(w, 30, 29)) return;
          switch (bits(w, 15, 10)) {
            case 2: i.code = Code::Udiv; return;
            case 3: i.code = Code::Sdiv; return;
            case 8: i.code = Code::Lslv; return;
            case 9: i.code = Code::Lsrv; return;
            case 10: i.code = Code::Asrv; return;
            case 11: i.code = Code::Rorv; return;
            default: return;
          }
        default: return;
      }
    case 0b11011:  // data-processing (3 source): MADD / MSUB only
      if (bits(w, 30, 29) || bits(w, 23, 21)) return;
      i.ra = static_cast<uint8_t>(bits(w, 14, 10));
      i.code = bits(w, 15, 15) ? Code::Msub : Code::Madd;
      return;
    default: return;
  }
}

}  // namespace detail

inline void decode_into(uint32_t w, Instruction& i) noexcept {
  i = Instruction{};
  i.word = w;
  if (w == 0) { i.code = Code::Halt; return; }
  switch (bits(w, 28, 25)) {
    case 0b1000: case 0b1001: detail::decode_dp_imm(w, i); return;
    case 0b1010: case 0b1011: detail::decode_branch_sys(w, i); return;
    case 0b0100: case 0b0110: case 0b1100: case 0b1110: detail::decode_ldst(w, i); return;
    case 0b0101: case 0b1101: detail::decode_dp_reg(w, i); return;
    default: return;  // Code::Invalid
  }
}

}  // namespace silica
