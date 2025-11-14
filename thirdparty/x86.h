/*
 * Copyright © 2022 Michael Smith <mikesmiffy128@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INC_X86_H
#define INC_X86_H

#ifdef __cplusplus
extern "C" {
#endif 
/*
 * Opcode-based X86 instruction analysis. In other words, *NOT* a disassembler.
 * Only cares about the instructions we expect to see in basic 32-bit userspace
 * functions; there's no kernel-mode instructions, no MMX/3DNow!/SSE/AVX, no
 * REX, EVEX, yadda yadda.
 */

// XXX: no BOUND (0x62): ambiguous with EVEX prefix - can't be arsed!

/* Instruction prefixes: segments */
#define X86_SEG_PREFIXES(X) \
	X(X86_PFX_ES,   0x26) \
	X(X86_PFX_CS,   0x2E) \
	X(X86_PFX_SS,   0x36) \
	X(X86_PFX_DS,   0x3E) \
	X(X86_PFX_FS,   0x64) \
	X(X86_PFX_GS,   0x65)

/* Instruction prefixes: operations */
#define X86_OP_PREFIXES(X) \
	X(X86_PFX_OPSZ, 0x66) \
	X(X86_PFX_ADSZ, 0x67) \
	X(X86_PFX_LOCK, 0xF0) \
	X(X86_PFX_REPN, 0xF2) \
	X(X86_PFX_REP,  0xF3)

/* All instruction prefixes */
#define X86_PREFIXES(X) X86_SEG_PREFIXES(X) X86_OP_PREFIXES(X)

/* Single-byte opcodes with no operands */
#define X86_OPS_1BYTE_NO(X) \
	X(X86_PUSHES,     0x06) \
	X(X86_POPES,      0x07) \
	X(X86_PUSHCS,     0x0E) \
	X(X86_PUSHSS,     0x16) \
	X(X86_POPSS,      0x17) \
	X(X86_PUSHDS,     0x1E) \
	X(X86_POPDS,      0x1F) \
	X(X86_DAA,        0x27) \
	X(X86_DAS,        0x2F) \
	X(X86_AAA,        0x37) \
	X(X86_AAS,        0x3F) \
	X(X86_INCEAX,     0x40) \
	X(X86_INCECX,     0x41) \
	X(X86_INCEDX,     0x42) \
	X(X86_INCEBX,     0x43) \
	X(X86_INCESP,     0x44) \
	X(X86_INCEBP,     0x45) \
	X(X86_INCESI,     0x46) \
	X(X86_INCEDI,     0x47) \
	X(X86_DECEAX,     0x48) \
	X(X86_DECECX,     0x49) \
	X(X86_DECEDX,     0x4A) \
	X(X86_DECEBX,     0x4B) \
	X(X86_DECESP,     0x4C) \
	X(X86_DECEBP,     0x4D) \
	X(X86_DECESI,     0x4E) \
	X(X86_DECEDI,     0x4F) \
	X(X86_PUSHEAX,    0x50) \
	X(X86_PUSHECX,    0x51) \
	X(X86_PUSHEDX,    0x52) \
	X(X86_PUSHEBX,    0x53) \
	X(X86_PUSHESP,    0x54) \
	X(X86_PUSHEBP,    0x55) \
	X(X86_PUSHESI,    0x56) \
	X(X86_PUSHEDI,    0x57) \
	X(X86_POPEAX,     0x58) \
	X(X86_POPECX,     0x59) \
	X(X86_POPEDX,     0x5A) \
	X(X86_POPEBX,     0x5B) \
	X(X86_POPESP,     0x5C) \
	X(X86_POPEBP,     0x5D) \
	X(X86_POPESI,     0x5E) \
	X(X86_POPEDI,     0x5F) \
	X(X86_PUSHA,      0x60) \
	X(X86_POPA,       0x61) \
	X(X86_NOP,        0x90) \
	X(X86_XCHGECXEAX, 0x91) \
	X(X86_XCHGEDXEAX, 0x92) \
	X(X86_XCHGEBXEAX, 0x93) \
	X(X86_XCHGESPEAX, 0x94) \
	X(X86_XCHGEBPEAX, 0x95) \
	X(X86_XCHGESIEAX, 0x96) \
	X(X86_XCHGEDIEAX, 0x97) \
	X(X86_CWDE,       0x98) \
	X(X86_CDQ,        0x99) \
	X(X86_WAIT,       0x9B) \
	X(X86_PUSHF,      0x9C) \
	X(X86_POPF,       0x9D) \
	X(X86_SAHF,       0x9E) \
	X(X86_LAHF,       0x9F) \
	X(X86_MOVS8,      0xA4) \
	X(X86_MOVSW,      0xA5) \
	X(X86_CMPS8,      0xA6) \
	X(X86_CMPSW,      0xA7) \
	X(X86_STOS8,      0xAA) \
	X(X86_STOSD,      0xAB) \
	X(X86_LODS8,      0xAC) \
	X(X86_LODSD,      0xAD) \
	X(X86_SCAS8,      0xAE) \
	X(X86_SCASD,      0xAF) \
	X(X86_RET,        0xC3) \
	X(X86_LEAVE,      0xC9) \
	X(X86_RETF,       0xCB) \
	X(X86_INT3,       0xCC) \
	X(X86_INTO,       0xCE) \
	X(X86_XLAT,       0xD7) \
	X(X86_CMC,        0xF5) \
	X(X86_CLC,        0xF8) \
	X(X86_STC,        0xF9) \
	X(X86_CLI,        0xFA) \
	X(X86_STI,        0xFB) \
	X(X86_CLD,        0xFC) \
	X(X86_STD,        0xFD)

/* Single-byte opcodes with a 1-byte immediate operand */
#define X86_OPS_1BYTE_I8(X) \
	X(X86_ADDALI,  0x04) \
	X(X86_ORALI,   0x0C) \
	X(X86_ADCALI,  0x14) \
	X(X86_SBBALI,  0x1C) \
	X(X86_ANDALI,  0x24) \
	X(X86_SUBALI,  0x2C) \
	X(X86_XORALI,  0x34) \
	X(X86_CMPALI,  0x3C) \
	X(X86_PUSHI8,  0x6A) \
	X(X86_MOVALII, 0xA0) /* From offset (indirect) */ \
	X(X86_MOVIIAL, 0xA2) /* To offset (indirect) */ \
	X(X86_TESTALI, 0xA8) \
	X(X86_JO,      0x70) \
	X(X86_JNO,     0x71) \
	X(X86_JB,      0x72) /* AKA JC */ \
	X(X86_JNB,     0x73) /* AKA JNC */ \
	X(X86_JZ,      0x74) /* AKA JE */ \
	X(X86_JNZ,     0x75) /* AKA JNZ */ \
	X(X86_JNA,     0x76) /* AKA JBE */ \
	X(X86_JA,      0x77) /* AKA JNBE */ \
	X(X86_JS,      0x78) \
	X(X86_JNS,     0x79) \
	X(X86_JP,      0x7A) \
	X(X86_JNP,     0x7B) \
	X(X86_JL,      0x7C) /* AKA JNGE */ \
	X(X86_JNL,     0x7D) /* AKA JGE */ \
	X(X86_JNG,     0x7E) /* AKA JLE */ \
	X(X86_JG,      0x7F) /* AKA JNLE */ \
	X(X86_MOVALI,  0xB0) \
	X(X86_MOVCLI,  0xB1) \
	X(X86_MOVDLI,  0xB2) \
	X(X86_MOVBLI,  0xB3) \
	X(X86_MOVAHI,  0xB4) \
	X(X86_MOVCHI,  0xB5) \
	X(X86_MOVDHI,  0xB6) \
	X(X86_MOVBHI,  0xB7) \
	X(X86_INT,     0xCD) \
	X(X86_AMX,     0xD4) /* Note: D4 0A is referred to as AAM */ \
	X(X86_ADX,     0xD5) /* Note: D4 0A is referred to as AAD */ \
	X(X86_LOOPNZ,  0xE0) /* AKA LOOPNE */ \
	X(X86_LOOPZ,   0xE1) /* AKA LOOPE */ \
	X(X86_LOOP,    0xE2) \
	X(X86_JCXZ,    0xE3) \
	X(X86_JMPI8,   0xEB)

/* Single-byte opcodes with a word-sized immediate operand */
#define X86_OPS_1BYTE_IW(X) \
	X(X86_ADDEAXI,  0x05) \
	X(X86_OREAXI,   0x0D) \
	X(X86_ADCEAXI,  0x15) \
	X(X86_SBBEAXI,  0x1D) \
	X(X86_ANDEAXI,  0x25) \
	X(X86_SUBEAXI,  0x2D) \
	X(X86_XOREAXI,  0x35) \
	X(X86_CMPEAXI,  0x3D) \
	X(X86_PUSHIW,   0x68) \
	X(X86_MOVEAXII, 0xA1) /* From offset (indirect) */ \
	X(X86_MOVIIEAX, 0xA3) /* To offset (indirect) */ \
	X(X86_TESTEAXI, 0xA9) \
	X(X86_MOVEAXI,  0xB8) \
	X(X86_MOVECXI,  0xB9) \
	X(X86_MOVEDXI,  0xBA) \
	X(X86_MOVEBXI,  0xBB) \
	X(X86_MOVESPI,  0xBC) \
	X(X86_MOVEBPI,  0xBD) \
	X(X86_MOVESII,  0xBE) \
	X(X86_MOVEDII,  0xBF) \
	X(X86_CALL,     0xE8) \
	X(X86_JMPIW,    0xE9)

/* Single-byte opcodes with 16-bit immediate operands, regardless of prefixes */
#define X86_OPS_1BYTE_I16(X) \
	X(X86_RETI16,  0xC2) \
	X(X86_RETFI16, 0xCA)

/*
 * Single-byte opcodes with a ModRM. `MR` in a name means the ModRM is the
 * destination, `RM` means it's the source.
 */
#define X86_OPS_1BYTE_MRM(X) \
	X(X86_ADDMR8,    0x00) \
	X(X86_ADDMRW,    0x01) \
	X(X86_ADDRM8,    0x02) \
	X(X86_ADDRMW,    0x03) \
	X(X86_ORMR8,     0x08) \
	X(X86_ORMRW,     0x09) \
	X(X86_ORRM8,     0x0A) \
	X(X86_ORRMW,     0x0B) \
	X(X86_ADCMR8,    0x10) \
	X(X86_ADCMRW,    0x11) \
	X(X86_ADCRM8,    0x12) \
	X(X86_ADCRMW,    0x13) \
	X(X86_SBBMR8,    0x18) \
	X(X86_SBBMRW,    0x19) \
	X(X86_SBBRM8,    0x1A) \
	X(X86_SBBRMW,    0x1B) \
	X(X86_ANDMR8,    0x20) \
	X(X86_ANDMRW,    0x21) \
	X(X86_ANDRM8,    0x22) \
	X(X86_ANDRMW,    0x23) \
	X(X86_SUBMR8,    0x28) \
	X(X86_SUBMRW,    0x29) \
	X(X86_SUBRM8,    0x2A) \
	X(X86_SUBRMW,    0x2B) \
	X(X86_XORMR8,    0x30) \
	X(X86_XORMRW,    0x31) \
	X(X86_XORRM8,    0x32) \
	X(X86_XORRMW,    0x33) \
	X(X86_CMPMR8,    0x38) \
	X(X86_CMPMRW,    0x39) \
	X(X86_CMPRM8,    0x3A) \
	X(X86_CMPRMW,    0x3B) \
	X(X86_ARPL,      0x63) \
	X(X86_TESTMR8,   0x84) \
	X(X86_TESTMRW,   0x85) \
	X(X86_XCHGMR8,   0x86) \
	X(X86_XCHGMRW,   0x87) \
	X(X86_MOVMR8,    0x88) \
	X(X86_MOVMRW,    0x89) \
	X(X86_MOVRM8,    0x8A) \
	X(X86_MOVRMW,    0x8B) \
	X(X86_MOVMS,     0x8C) /* Load 4 bytes from segment register */ \
	X(X86_LEA,       0x8D) \
	X(X86_MOVSM,     0x8E) /* Store 4 bytes to segment register */ \
	X(X86_POPM,      0x8F) \
	X(X86_LES,       0xC4) \
	X(X86_LDS,       0xC5) \
	X(X86_SHIFTM18,  0xD0) /* Shift/roll by 1 place */ \
	X(X86_SHIFTM1W,  0xD1) /* Shift/roll by 1 place */ \
	X(X86_SHIFTMCL8, 0xD2) /* Shift/roll by CL places */ \
	X(X86_SHIFTMCLW, 0xD3) /* Shift/roll by CL places */ \
	X(X86_FLTBLK1,   0xD8) /* Various float ops (1/8) */ \
	X(X86_FLTBLK2,   0xD9) /* Various float ops (2/8) */ \
	X(X86_FLTBLK3,   0xDA) /* Various float ops (3/8) */ \
	X(X86_FLTBLK4,   0xDB) /* Various float ops (4/8) */ \
	X(X86_FLTBLK5,   0xDC) /* Various float ops (5/8) */ \
	X(X86_FLTBLK6,   0xDD) /* Various float ops (6/8) */ \
	X(X86_FLTBLK7,   0xDE) /* Various float ops (7/8) */ \
	X(X86_FLTBLK8,   0xDF) /* Various float ops (8/8) */ \
	X(X86_MISCM8,    0xFE) /* Only documented for MRM.reg in {0, 1} */ \
	X(X86_MISCMW,    0xFF)

/* Single-byte opcodes with a ModRM and a 1-byte immediate operand */
#define X86_OPS_1BYTE_MRM_I8(X) \
	X(X86_IMULMI8,  0x6B) /* 3-operand multiply */ \
	X(X86_ALUMI8,   0x80) /* ALU op in MRM.reg, from immediate */ \
	X(X86_ALUMI8X,  0x82) /* ALU op in MRM.reg, from immediate, redundant?? */ \
	X(X86_ALUMI8S,  0x83) /* ALU op in MRM.reg, from immediate, sign-extend */ \
	X(X86_SHIFTMI8, 0xC0) /* Shift/roll by imm8 places */ \
	X(X86_SHIFTMIW, 0xC1) /* Shift/roll by imm8 places */ \
	X(X86_MOVMI8,   0xC6) /* Note: RM.reg must be 0 */

/* Single-byte opcodes with a ModRM and a word-sized immediate operand */
#define X86_OPS_1BYTE_MRM_IW(X) \
	X(X86_IMULMIW, 0x69) /* 3-operand multiply */ \
	X(X86_ALUMIW,  0x81) /* ALU op in MRM.reg, from immediate */ \
	X(X86_MOVMIW,  0xC7) /* Note: MRM.reg must be 0 */

/* All single-byte x86 instructions */
#define X86_OPS_1BYTE(X) \
	X86_OPS_1BYTE_NO(X) \
	X86_OPS_1BYTE_I8(X) \
	X86_OPS_1BYTE_IW(X) \
	X86_OPS_1BYTE_I16(X) \
	X86_OPS_1BYTE_MRM(X) \
	X86_OPS_1BYTE_MRM_I8(X) \
	X86_OPS_1BYTE_MRM_IW(X) \
	X(X86_ENTER,  0xC8) /* Dumb special case insn: imm16 followed by imm8 */ \
	X(X86_CRAZY8, 0xF6) /* CRAZY reg-encoded block, has imm8 IFF reg < 2 */ \
	X(X86_CRAZYW, 0xF7) /* CRAZY reg-encoded block, has imm32/16 IFF reg < 2 */

/* Second bytes of opcodes with no operands */
#define X86_OPS_2BYTE_NO(X) \
	X(X86_2B_RDTSC,    0x31) \
	X(X86_2B_RDPMD,    0x33) \
	X(X86_2B_SYSENTER, 0x34) \
	X(X86_2B_PUSHFS,   0xA0) \
	X(X86_2B_POPFS,    0xA1) \
	X(X86_2B_CPUID,    0xA2) \
	X(X86_2B_PUSHGS,   0xA8) \
	X(X86_2B_POPGS,    0xA9) \
	X(X86_2B_RSM,      0xAA) \
	X(X86_2B_BSWAPEAX,  0xC8) \
	X(X86_2B_BSWAPECX,  0xC9) \
	X(X86_2B_BSWAPEDX,  0xCA) \
	X(X86_2B_BSWAPEBX,  0xCB) \
	X(X86_2B_BSWAPESP,  0xCC) \
	X(X86_2B_BSWAPEBP,  0xCD) \
	X(X86_2B_BSWAPESI,  0xCE) \
	X(X86_2B_BSWAPEDI,  0xCF)

/* Second bytes of opcodes with a word-sized immediate operand */
#define X86_OPS_2BYTE_IW(X) \
	X(X86_2B_JOII,  0x80) /* From offset (indirect) */ \
	X(X86_2B_JNOII, 0x81) /* From offset (indirect) */ \
	X(X86_2B_JBII,  0x82) /* AKA JC; from offset (indirect) */ \
	X(X86_2B_JNBII, 0x83) /* AKA JNC; from offset (indirect) */ \
	X(X86_2B_JZII,  0x84) /* AKA JE; from offset (indirect) */ \
	X(X86_2B_JNZII, 0x85) /* AKA JNZ; from offset (indirect) */ \
	X(X86_2B_JNAII, 0x86) /* AKA JBE; from offset (indirect) */ \
	X(X86_2B_JAII,  0x87) /* AKA JNBE; from offset (indirect) */ \
	X(X86_2B_JSII,  0x88) /* From offset (indirect) */ \
	X(X86_2B_JNSII, 0x89) /* From offset (indirect) */ \
	X(X86_2B_JPII,  0x8A) /* From offset (indirect) */ \
	X(X86_2B_JNPII, 0x8B) /* From offset (indirect) */ \
	X(X86_2B_JLII,  0x8C) /* AKA JNGE; from offset (indirect) */ \
	X(X86_2B_JNLII, 0x8D) /* AKA JGE; from offset (indirect) */ \
	X(X86_2B_JNGII, 0x8E) /* AKA JLE; from offset (indirect) */ \
	X(X86_2B_JGII,  0x8F) /* AKA JNLE; from offset (indirect) */

/* Second bytes of opcodes with a ModRM */
#define X86_OPS_2BYTE_MRM(X) \
	X(X86_2B_NOP,      0x0D) /* Variable length NOP (3-9 with prefix) */ \
	X(X86_2B_HINTS1,   0x18) /* Prefetch and hint-nop block 1/8 */ \
	X(X86_2B_HINTS2,   0x19) /* Prefetch and hint-nop block 2/8 */ \
	X(X86_2B_HINTS3,   0x1A) /* Prefetch and hint-nop block 3/8 */ \
	X(X86_2B_HINTS4,   0x1B) /* Prefetch and hint-nop block 4/8 */ \
	X(X86_2B_HINTS5,   0x1C) /* Prefetch and hint-nop block 5/8 */ \
	X(X86_2B_HINTS6,   0x1D) /* Prefetch and hint-nop block 6/8 */ \
	X(X86_2B_HINTS7,   0x1E) /* Prefetch and hint-nop block 7/8 */ \
	X(X86_2B_HINTS8,   0x1F) /* Prefetch and hint-nop block 8/8 */ \
	X(X86_2B_CMOVO,    0x40) \
	X(X86_2B_CMOVNO,   0x41) \
	X(X86_2B_CMOVB,    0x42) /* AKA CMOVC */ \
	X(X86_2B_CMOVNB,   0x43) /* AKA CMOVNC */ \
	X(X86_2B_CMOVZ,    0x44) /* AKA CMOVE */ \
	X(X86_2B_CMOVNZ,   0x45) /* AKA CMOVNE */ \
	X(X86_2B_CMOVNA,   0x46) /* AKA CMOVBE */ \
	X(X86_2B_CMOVA,    0x47) /* AKA CMOVNBE */ \
	X(X86_2B_CMOVS,    0x48) \
	X(X86_2B_CMOVNS,   0x49) \
	X(X86_2B_CMOVP,    0x4A) \
	X(X86_2B_CMOVNP,   0x4B) \
	X(X86_2B_CMOVL,    0x4C) /* AKA CMOVNGE */ \
	X(X86_2B_CMOVNL,   0x4D) /* AKA CMOVGE */ \
	X(X86_2B_CMOVNG,   0x4E) /* AKA CMOVLE */ \
	X(X86_2B_CMOVG,    0x4F) /* AKA CMOVNLE */ \
	X(X86_2B_SETO,     0x90) \
	X(X86_2B_SETNO,    0x91) \
	X(X86_2B_SETB,     0x92) /* AKA SETC */ \
	X(X86_2B_SETNB,    0x93) /* AKA SETNC */ \
	X(X86_2B_SETZ,     0x94) /* AKA SETE */ \
	X(X86_2B_SETNZ,    0x95) /* AKA SETNZ */ \
	X(X86_2B_SETNA,    0x96) /* AKA SETBE */ \
	X(X86_2B_SETA,     0x97) /* AKA SETNBE */ \
	X(X86_2B_SETS,     0x98) \
	X(X86_2B_SETNS,    0x99) \
	X(X86_2B_SETP,     0x9A) \
	X(X86_2B_SETNP,    0x9B) \
	X(X86_2B_SETL,     0x9C) /* AKA SETNGE */ \
	X(X86_2B_SETNL,    0x9D) /* AKA SETGE */ \
	X(X86_2B_SETNG,    0x9E) /* AKA SETLE */ \
	X(X86_2B_SETG,     0x9F) /* AKA SETNLE */ \
	X(X86_2B_BTMR,     0xA3) \
	X(X86_2B_SHLDMRCL, 0xA5) \
	X(X86_2B_BTS,      0xAB) \
	X(X86_2B_SHRDMRCL, 0xAD) \
	X(X86_2B_MISC,     0xAE) /* Float env stuff, memory fences */ \
	X(X86_2B_IMUL,     0xAF) \
	X(X86_2B_CMPXCHG8, 0xB0) \
	X(X86_2B_CMPXCHGW, 0xB1) \
	X(X86_2B_MOVZX8,   0xB6) \
	X(X86_2B_MOVZXW,   0xB7) \
	X(X86_2B_POPCNT,   0xB8) \
	X(X86_2B_BTCRM,    0xBB) \
	X(X86_2B_BSF,      0xBC) \
	X(X86_2B_BSR,      0xBD) \
	X(X86_2B_MOVSX8,   0xBE) \
	X(X86_2B_MOVSXW,   0xBF) \
	X(X86_2B_XADDRM8,  0xC0) \
	X(X86_2B_XADDRMW,  0xC1) \
	/* NOTE: this one is actually a block with some VMX stuff too; it's only
	   CMPXCHG64 (CMPXCHG8B if you prefer) if MRM.reg = 1, but naming it this
	   way seemed more useful since it's what you'll see in normal userspace
	   programs, which is what we're interested in. */ \
	X(X86_2B_CMPXCHG64, 0xC7)

/* Second bytes of opcodes with a ModRM and a 1-byte immediate operand */
#define X86_OPS_2BYTE_MRM_I8(X) \
	X(X86_2B_SHLDMRI, 0xA4) \
	X(X86_2B_SHRDMRI, 0xAC) \
	X(X86_2B_BTXMI,   0xBA) /* BT/BTS/BTR/BTC depending on MRM.reg (4-7) */

#define X86_OPS_2BYTE(X) \
	X86_OPS_2BYTE_NO(X) \
	X86_OPS_2BYTE_IW(X) \
	X86_OPS_2BYTE_MRM(X) \
	X86_OPS_2BYTE_MRM_I8(X)

#define _X86_ENUM(name, value) name = value,
enum {
	X86_PREFIXES(_X86_ENUM)
	X86_OPS_1BYTE(_X86_ENUM)
	X86_2BYTE = 0x0F, /* First byte of a 2- or 3-byte opcode */
	X86_OPS_2BYTE(_X86_ENUM)
	X86_3BYTE1 = 0x38, /* One of the two second bytes of a three-byte opcode */
	X86_3BYTE2 = 0x3A, /* The other second byte of a three-byte opcode */
	X86_3DNOW  = 0x0F /* The second byte of a three-byte 3DNow! opcode */
};
#undef _X86_ENUM

/*
 * Returns the length of an instruction, or -1 if it's a "known unknown" or
 * invalid instruction. Doesn't handle unknown unknowns: may explode or hang on
 * arbitrary untrusted data. Also doesn't handle, among other things, 3DNow!,
 * SSE, MMX, AVX, and such. Aims to be small and fast rather than comprehensive.
 */
int x86_len(const void *insn);

/* Constructs a ModRM byte, assuming the parameters are all in range. */
#define X86_MODRM(mod, reg, rm) (unsigned char)((mod) << 6 | (reg) << 3 | rm)

#ifdef __cplusplus
}
#endif

#endif

// vi: sw=4 ts=4 noet tw=80 cc=80
