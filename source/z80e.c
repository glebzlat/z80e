#include "z80e.h"

#define MAXU8 ((u8) - 1)
#define MAXI8 ((i8) - 129)
#define MINI8 ((i8)128)

#define reg(name) (z80->reg.cur->name)

#define bit(v, n) (((v) & (1 << n)) != 0)
#define low_nibble(v) (v & 0x0f)
#define high_nibble(v) (v >> 4)

static u8 z80e_execute(z80e* z80, u8 opcode);

static u8 dec8(z80e* z80, u8* reg);
static u8 inc8(z80e* z80, u8* reg);
static u8 add8(z80e* z80, u8 v);
static u8 sub8(z80e* z80, u8 v);
static u8 and8(z80e* z80, u8 v);
static u8 xor8(z80e* z80, u8 v);
static u16 add16(z80e* z80, u16 a, u16 b);

static u8 is_even_parity(u8 v);

static u8 jr(z80e* z80, u8 cond);
static void daa(z80e* z80);

/* Sign flag */
static inline u8 sf(z80e* z80) { return (reg(f) & (1 << 7)); }
/* Zero flag */
static inline u8 zf(z80e* z80) { return (reg(f) & (1 << 6)); }
/* Y flag - a copy of bit 5 of the result */
static inline u8 yf(z80e* z80) { return (reg(f) & (1 << 5)); }
/* Half-carry flag */
static inline u8 hf(z80e* z80) { return (reg(f) & (1 << 4)); }
/* X flag - a copy of bit 3 of the result */
static inline u8 xf(z80e* z80) { return (reg(f) & (1 << 3)); }
/* Parity/Overflow flag */
static inline u8 pof(z80e* z80) { return (reg(f) & (1 << 2)); }
/* Add/Subtract flag */
static inline u8 nf(z80e* z80) { return (reg(f) & (1 << 1)); }
/* Carry flag */
static inline u8 cf(z80e* z80) { return (reg(f) & (1 << 0)); }

static inline void set_f(z80e* z80, u8 v, u8 pos) {
  if (v) {
    reg(f) |= 1 << pos;
  } else {
    reg(f) &= (1 << pos) ^ 0xff;
  }
}

static inline void set_sf(z80e* z80, u8 v) { set_f(z80, v, 7); }
static inline void set_zf(z80e* z80, u8 v) { set_f(z80, v, 6); }
static inline void set_yf(z80e* z80, u8 v) { set_f(z80, v, 5); }
static inline void set_hf(z80e* z80, u8 v) { set_f(z80, v, 4); }
static inline void set_xf(z80e* z80, u8 v) { set_f(z80, v, 3); }
static inline void set_pof(z80e* z80, u8 v) { set_f(z80, v, 2); }
static inline void set_nf(z80e* z80, u8 v) { set_f(z80, v, 1); }
static inline void set_cf(z80e* z80, u8 v) { set_f(z80, v, 0); }

static inline void set_yf_u8(z80e* z80, u8 v) { set_yf(z80, bit(v, 5)); }
static inline void set_xf_u8(z80e* z80, u8 v) { set_xf(z80, bit(v, 3)); }
static inline void set_yf_u16(z80e* z80, u16 v) { set_yf(z80, bit(v, 13)); }
static inline void set_xf_u16(z80e* z80, u16 v) { set_xf(z80, bit(v, 11)); }

inline static int u8_overflow(u8 a, u8 b) { return b > 0 && a > 0xff - b; }
inline static int u16_overflow(u16 a, u16 b) { return b > 0 && a > 0xffff - b; }
inline static int u8_half_carry(u8 a, u8 b) { return ((a & 0x0f) + (b & 0x0f)) > 0x0f; }
inline static int u8_half_borrow(u8 a, u8 b) { return (a & 0x0f) < (b & 0x0f); }
inline static int u16_byte_carry(u16 a, u16 b) { return ((a & 0x00ff) + (b & 0x00ff)) > 0x00ff; }
inline static int u8_negative(u8 i) { return bit(i, 7); }

static u8 read_byte(z80e* z80);
static u8 read_byte_at(z80e* z80, u16 addr);
static u16 read_word(z80e* z80);
static u16 read_word_at(z80e* z80, u16 addr);
static void write_byte(z80e* z80, u8 byte);
static void write_byte_at(z80e* z80, u8 byte, u16 addr);
static void write_word(z80e* z80, u16 word);

static inline u16 bc(z80e* z80) { return (reg(b) << 8) | reg(c); }
static inline u16 hl(z80e* z80) { return (reg(h) << 8) | reg(l); }
static inline u16 de(z80e* z80) { return (reg(d) << 8) | reg(e); }

static void set_bc(z80e* z80, u16 val);
static void set_hl(z80e* z80, u16 val);
static void set_de(z80e* z80, u16 val);

void z80e_init(z80e* z80, z80e_config* config) {
  for (uint32_t i = 0; i < sizeof(*z80); ++i) {
    ((u8*)z80)[i] = 0;
  }

  z80->memread = config->memread;
  z80->memwrite = config->memwrite;
  z80->ioread = config->ioread;
  z80->iowrite = config->iowrite;
  z80->ctx = config->ctx;

  z80->reg.cur = &z80->reg.main;
}

int8_t z80e_instruction(z80e* z80) {
  if (z80->error) {
    return z80->error;
  }

  if (z80->halt) {
    return 4;
  }

  u8 opcode = read_byte(z80);
  i8 ret = z80e_execute(z80, opcode);
  if (ret < 0) {
    z80->error = ret;
  }

  return ret;
}

void z80e_halt(z80e* z80) { z80->halt = 1; }

int z80e_get_halt(z80e* z80) { return z80->halt; }

static u8 z80e_execute(z80e* z80, u8 opcode) {
  u8 tmp8;

  switch (opcode) {
    /* clang-format off */
  case 0x78: reg(a) = reg(b); return 4; /* ld a, b */
  case 0x79: reg(a) = reg(c); return 4; /* ld a, c */
  case 0x7a: reg(a) = reg(d); return 4; /* ld a, d */
  case 0x7b: reg(a) = reg(e); return 4; /* ld a, e */
  case 0x7c: reg(a) = reg(h); return 4; /* ld a, h */
  case 0x7d: reg(a) = reg(l); return 4; /* ld a, l */
  case 0x7f: reg(a) = reg(a); return 4; /* ld a, a */
  case 0x3e: reg(a) = read_byte(z80); return 7; /* ld a, n */
  case 0x7e: reg(a) = read_byte_at(z80, hl(z80)); return 7; /* ld a, (hl) */
  case 0x0a: reg(a) = read_byte_at(z80, bc(z80)); return 7; /* ld a, (bc) */
  case 0x1a: reg(a) = read_byte_at(z80, de(z80)); return 7; /* ld a, (de) */
  case 0x3a: reg(a) = read_byte_at(z80, read_word(z80)); return 13; /* ld a, (nn) */

  case 0x40: reg(b) = reg(b); return 4; /* ld b, b */
  case 0x41: reg(b) = reg(c); return 4; /* ld b, c */
  case 0x42: reg(b) = reg(d); return 4; /* ld b, d */
  case 0x43: reg(b) = reg(e); return 4; /* ld b, e */
  case 0x44: reg(b) = reg(h); return 4; /* ld b, h */
  case 0x45: reg(b) = reg(l); return 4; /* ld b, l */
  case 0x47: reg(b) = reg(a); return 4; /* ld b, a */
  case 0x06: reg(b) = read_byte(z80); return 7; /* ld b, n */
  case 0x46: reg(b) = read_byte_at(z80, hl(z80)); return 7; /* ld b, (hl) */

  case 0x48: reg(c) = reg(b); return 4; /* ld c, b */
  case 0x49: reg(c) = reg(c); return 4; /* ld c, c */
  case 0x4a: reg(c) = reg(d); return 4; /* ld c, d */
  case 0x4b: reg(c) = reg(e); return 4; /* ld c, e */
  case 0x4c: reg(c) = reg(h); return 4; /* ld c, h */
  case 0x4d: reg(c) = reg(l); return 4; /* ld c, l */
  case 0x4f: reg(c) = reg(a); return 4; /* ld c, a */
  case 0x0e: reg(c) = read_byte(z80); return 7; /* ld c, n */
  case 0x4e: reg(c) = read_byte_at(z80, hl(z80)); return 7; /* ld c, (hl) */

  case 0x50: reg(d) = reg(b); return 4; /* ld d, b */
  case 0x51: reg(d) = reg(c); return 4; /* ld d, c */
  case 0x52: reg(d) = reg(d); return 4; /* ld d, d */
  case 0x53: reg(d) = reg(e); return 4; /* ld d, e */
  case 0x54: reg(d) = reg(h); return 4; /* ld d, h */
  case 0x55: reg(d) = reg(l); return 4; /* ld d, l */
  case 0x57: reg(d) = reg(a); return 4; /* ld d, a */
  case 0x16: reg(d) = read_byte(z80); return 7; /* ld d, n */
  case 0x56: reg(d) = read_byte_at(z80, hl(z80)); return 7; /* ld d, (hl) */

  case 0x58: reg(e) = reg(b); return 4; /* ld e, b */
  case 0x59: reg(e) = reg(c); return 4; /* ld e, c */
  case 0x5a: reg(e) = reg(d); return 4; /* ld e, d */
  case 0x5b: reg(e) = reg(e); return 4; /* ld e, e */
  case 0x5c: reg(e) = reg(h); return 4; /* ld e, h */
  case 0x5d: reg(e) = reg(l); return 4; /* ld e, l */
  case 0x5f: reg(e) = reg(a); return 4; /* ld e, a */
  case 0x1e: reg(e) = read_byte(z80); return 7; /* ld e, n */
  case 0x5e: reg(e) = read_byte_at(z80, hl(z80)); return 6; /* ld e, (hl) */

  case 0x60: reg(h) = reg(b); return 4; /* ld h, b */
  case 0x61: reg(h) = reg(c); return 4; /* ld h, c */
  case 0x62: reg(h) = reg(d); return 4; /* ld h, d */
  case 0x63: reg(h) = reg(e); return 4; /* ld h, e */
  case 0x64: reg(h) = reg(h); return 4; /* ld h, h */
  case 0x65: reg(h) = reg(l); return 4; /* ld h, l */
  case 0x67: reg(h) = reg(a); return 4; /* ld h, a */
  case 0x26: reg(h) = read_byte(z80); return 7; /* ld h, n */
  case 0x66: reg(h) = read_byte_at(z80, hl(z80)); return 7; /* ld h, (hl) */

  case 0x68: reg(l) = reg(b); return 4; /* ld l, b */
  case 0x69: reg(l) = reg(c); return 4; /* ld l, c */
  case 0x6a: reg(l) = reg(d); return 4; /* ld l, d */
  case 0x6b: reg(l) = reg(e); return 4; /* ld l, e */
  case 0x6c: reg(l) = reg(h); return 4; /* ld l, h */
  case 0x6d: reg(l) = reg(l); return 4; /* ld l, l */
  case 0x6f: reg(l) = reg(a); return 4; /* ld l, a */
  case 0x2e: reg(l) = read_byte(z80); return 7; /* ld l, n */
  case 0x6e: reg(l) = read_byte_at(z80, hl(z80)); return 6; /* ld l, (hl) */

  case 0x70: write_byte_at(z80, reg(b), hl(z80)); return 7; /* ld (hl), b */
  case 0x71: write_byte_at(z80, reg(c), hl(z80)); return 7; /* ld (hl), c */
  case 0x72: write_byte_at(z80, reg(d), hl(z80)); return 7; /* ld (hl), d */
  case 0x73: write_byte_at(z80, reg(e), hl(z80)); return 7; /* ld (hl), e */
  case 0x74: write_byte_at(z80, reg(h), hl(z80)); return 7; /* ld (hl), h */
  case 0x75: write_byte_at(z80, reg(l), hl(z80)); return 7; /* ld (hl), l */
  case 0x77: write_byte_at(z80, reg(a), hl(z80)); return 7; /* ld (hl), a */
  case 0x36: write_byte_at(z80, read_byte(z80), hl(z80)); return 10; /* ld (hl), n */

  case 0x32: write_byte_at(z80, reg(a), read_word(z80)); return 13; /* ld (nn), a */

  case 0x12: write_byte_at(z80, reg(a), de(z80)); return 7; /* ld (de), a */

  case 0x2a: set_hl(z80, read_word_at(z80, read_word(z80))); return 16; /* ld hl, (nn) */
  case 0x21: set_hl(z80, read_word(z80)); return 10; /* ld hl, nn */

  case 0x02: set_bc(z80, reg(a)); return 7; /* ld bc, a */
  case 0x01: set_bc(z80, read_word(z80)); return 10; /* ld bc, nn */
  case 0x11: set_de(z80, read_word(z80)); return 10; /* ld de, nn */
  case 0x31: z80->reg.sp = read_word(z80); return 10; /* ld sp, nn */

  case 0x22: write_word(z80, hl(z80)); return 16; /* ld (nn), hl */

  case 0x04: return inc8(z80, &reg(b)); /* inc b */
  case 0x0c: return inc8(z80, &reg(c)); /* inc c */
  case 0x14: return inc8(z80, &reg(d)); /* inc d */
  case 0x1c: return inc8(z80, &reg(e)); /* inc e */
  case 0x24: return inc8(z80, &reg(h)); /* inc h */
  case 0x2c: return inc8(z80, &reg(l)); /* inc l */
  case 0x3c: return inc8(z80, &reg(a)); /* inc a */

  case 0x05: return dec8(z80, &reg(b)); /* dec b */
  case 0x0d: return dec8(z80, &reg(c)); /* dec c */
  case 0x15: return dec8(z80, &reg(d)); /* dec d */
  case 0x1d: return dec8(z80, &reg(e)); /* dec e */
  case 0x25: return dec8(z80, &reg(h)); /* dec h */
  case 0x2d: return dec8(z80, &reg(l)); /* dec l */
  case 0x3d: return dec8(z80, &reg(a)); /* dec a */

  case 0x33: z80->reg.sp += 1; return 6; /* inc sp */
  case 0x23: set_hl(z80, hl(z80) + 1); return 6; /* inc hl */
  case 0x13: set_de(z80, de(z80) + 1); return 6; /* inc de */

  case 0x3b: z80->reg.sp -= 1; return 6; /* dec sp */
  case 0x2b: set_hl(z80, hl(z80) - 1); return 6; /* dec hl */
  case 0x1b: set_de(z80, de(z80) - 1); return 6; /* dec de */

  case 0x80: return add8(z80, reg(b)); /* add a, b */
  case 0x81: return add8(z80, reg(c)); /* add a, c */
  case 0x82: return add8(z80, reg(d)); /* add a, d */
  case 0x83: return add8(z80, reg(e)); /* add a, e */
  case 0x84: return add8(z80, reg(h)); /* add a, h */
  case 0x85: return add8(z80, reg(l)); /* add a, l */
  case 0x87: return add8(z80, reg(a)); /* add a, a */
  case 0x86: return add8(z80, read_byte_at(z80, hl(z80))) + 3; /* add a, (hl) */

  case 0x88: return add8(z80, reg(b) + cf(z80)); /* adc a, b */
  case 0x89: return add8(z80, reg(c) + cf(z80)); /* adc a, c */
  case 0x8a: return add8(z80, reg(d) + cf(z80)); /* adc a, d */
  case 0x8b: return add8(z80, reg(e) + cf(z80)); /* adc a, e */
  case 0x8c: return add8(z80, reg(h) + cf(z80)); /* adc a, h */
  case 0x8d: return add8(z80, reg(l) + cf(z80)); /* adc a, l */
  case 0x8f: return add8(z80, reg(a) + cf(z80)); /* adc a, a */
  case 0x8e: return add8(z80, read_byte_at(z80, hl(z80)) + cf(z80)); /* adc a, (hl) */

  case 0x90: return sub8(z80, reg(b)); /* sub b */
  case 0x91: return sub8(z80, reg(c)); /* sub c */
  case 0x92: return sub8(z80, reg(d)); /* sub d */
  case 0x93: return sub8(z80, reg(e)); /* sub e */
  case 0x94: return sub8(z80, reg(h)); /* sub h */
  case 0x95: return sub8(z80, reg(l)); /* sub l */
  case 0x96: return sub8(z80, read_byte_at(z80, hl(z80))) + 3; /* sub (hl) */

  case 0x97: return sub8(z80, reg(a)); /* sub a */
  case 0x98: return sub8(z80, reg(b) + cf(z80)); /* sbc a, b */
  case 0x99: return sub8(z80, reg(c) + cf(z80)); /* sbc a, c */
  case 0x9a: return sub8(z80, reg(d) + cf(z80)); /* sbc a, d */
  case 0x9b: return sub8(z80, reg(e) + cf(z80)); /* sbc a, e */
  case 0x9c: return sub8(z80, reg(h) + cf(z80)); /* sbc a, h */
  case 0x9d: return sub8(z80, reg(l) + cf(z80)); /* sbc a, l */
  case 0x9f: return sub8(z80, reg(a) + cf(z80)); /* sbc a, a */
  case 0x9e: return sub8(z80, read_byte_at(z80, hl(z80)) + cf(z80)) + 3; /* sbc a, (hl) */

  case 0xa0: return and8(z80, reg(b)); /* and b */
  case 0xa1: return and8(z80, reg(c)); /* and c */
  case 0xa2: return and8(z80, reg(d)); /* and d */
  case 0xa3: return and8(z80, reg(e)); /* and e */
  case 0xa4: return and8(z80, reg(h)); /* and h */
  case 0xa5: return and8(z80, reg(l)); /* and l */
  case 0xa7: return and8(z80, reg(a)); /* and a */
  case 0xa6: return and8(z80, read_byte_at(z80, hl(z80))) + 3; /* and (hl) */

  case 0xa8: return xor8(z80, reg(b)); /* xor b */
  case 0xa9: return xor8(z80, reg(c)); /* xor c */
  case 0xaa: return xor8(z80, reg(d)); /* xor d */
  case 0xab: return xor8(z80, reg(e)); /* xor e */
  case 0xac: return xor8(z80, reg(h)); /* xor h */
  case 0xad: return xor8(z80, reg(l)); /* xor l */
  case 0xaf: return xor8(z80, reg(a)); /* xor a */
  case 0xae: return xor8(z80, read_byte_at(z80, hl(z80))) + 3; /* xor (hl) */

  case 0x29: set_hl(z80, add16(z80, hl(z80), hl(z80))); return 11; /* add hl, hl */
  case 0x39: set_hl(z80, add16(z80, hl(z80), z80->reg.sp)); return 11; /* add hl, sp */
  case 0x19: set_hl(z80, add16(z80, hl(z80), de(z80))); return 11; /* add hl, de */
  case 0x09: set_hl(z80, add16(z80, hl(z80), bc(z80))); return 11; /* add hl, bc */

  case 0x10: reg(b) = reg(b) - 1; return jr(z80, reg(b) != 0) + 1; /* djnz d */
  case 0x18: return jr(z80, 1); /* jr d */
  case 0x28: return jr(z80, zf(z80)); /* jr z, d */
  case 0x20: return jr(z80, !zf(z80)); /* jr nz, d */
  case 0x38: return jr(z80, cf(z80)); /* jr c, d */
  case 0x30: return jr(z80, !cf(z80)); /* jr nc, d */

  case 0x27: daa(z80); return 4; /* daa */
  case 0x2f: reg(a) = ~reg(a); return 4; /* cpl */

  case 0x3f: set_cf(z80, !cf(z80)); return 4; /* ccf */
  case 0x37: set_cf(z80, 1); return 4; /* scf */
  case 0x00: return 4; /* nop */
  case 0x76: z80->halt = 1; return 4; /* halt */

    /* clang-format on */

  case 0x03: /* inc bc */
    set_bc(z80, bc(z80) + 1);
    set_yf_u16(z80, bc(z80));
    set_xf_u16(z80, bc(z80));
    return 6;

  case 0x07: /* rlca */
    set_cf(z80, (reg(a) & 0x80) != 0);
    reg(a) = (reg(a) << 1) | cf(z80);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, bit(reg(a), 5));
    set_xf(z80, bit(reg(a), 3));
    return 4;

  case 0x0f: /* rrca */
    set_cf(z80, (reg(a) & 0x01) != 0);
    set_nf(z80, 0);
    set_hf(z80, 0);
    reg(a) = (reg(a) >> 1) | (cf(z80) << 7);
    return 4;

  case 0x08: /* ex af, af' */
    tmp8 = z80->reg.main.a;
    z80->reg.alt.a = z80->reg.main.a;
    z80->reg.main.a = tmp8;
    tmp8 = z80->reg.main.f;
    z80->reg.alt.f = z80->reg.main.f;
    z80->reg.main.f = tmp8;
    return 4;

  case 0x0b: /* dec bc */
    set_bc(z80, bc(z80) - 1);
    set_yf_u16(z80, bc(z80));
    set_xf_u16(z80, bc(z80));
    return 6;

  case 0x17: /* rla */
    tmp8 = (reg(a) & 0x80) != 0;
    reg(a) = (reg(a) << 1) | (cf(z80) & 0x01);
    set_cf(z80, tmp8);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, bit(reg(a), 5));
    set_xf(z80, bit(reg(a), 3));
    return 4;

  case 0x1f: /* rra */
    set_cf(z80, bit(reg(a), 0));
    reg(a) = (cf(z80) << 7) | (reg(a) >> 1);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, bit(reg(a), 5));
    set_xf(z80, bit(reg(a), 3));
    return 4;

  case 0x34: /* inc (hl) */
    tmp8 = read_byte_at(z80, hl(z80));
    inc8(z80, &tmp8);
    write_byte_at(z80, tmp8, hl(z80));
    return 11;

  case 0x35: /* dec (hl) */
    tmp8 = read_byte_at(z80, hl(z80));
    inc8(z80, &tmp8);
    write_byte_at(z80, tmp8, hl(z80));
    return 11;

  case 0xd9: /* exx */
    z80->reg.cur = z80->cur_reg_set == 0 ? &z80->reg.main : &z80->reg.alt;
    z80->cur_reg_set = !z80->cur_reg_set;
    return 4;

  default:
    return Z80E_INVALID_OPCODE;
  };
}

static u8 dec8(z80e* z80, u8* reg) {
  set_hf(z80, u8_half_borrow(*reg, 1));
  set_pof(z80, *reg == 0x80); /* P/V is set if m was 80h before operation */
  *reg -= 1;
  set_sf(z80, u8_negative(*reg));
  set_zf(z80, *reg == 0);
  set_nf(z80, 1); /* Add = 0/Subtract = 1 */
  set_yf(z80, bit(*reg, 5));
  set_xf(z80, bit(*reg, 3));
  return 4;
}

static u8 inc8(z80e* z80, u8* reg) {
  set_cf(z80, u8_overflow(*reg, 1));
  set_pof(z80, cf(z80));
  set_hf(z80, u8_half_carry(*reg, 1));
  *reg += 1;
  set_sf(z80, u8_negative(*reg));
  set_zf(z80, *reg == 0);
  set_yf(z80, bit(*reg, 5));
  set_xf(z80, bit(*reg, 3));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
  return 4;
}

static u8 add8(z80e* z80, u8 v) {
  set_cf(z80, u8_overflow(reg(a), v));
  set_hf(z80, u8_half_carry(reg(a), v));
  reg(a) += v;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_yf(z80, bit(reg(a), 3));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
  return 4;
}

static u8 sub8(z80e* z80, u8 v) {
  set_hf(z80, u8_half_borrow(reg(a), v)); /* Will borrow from bit 3 */
  set_pof(z80, reg(a) < v);              /* Will overflow */
  set_cf(z80, reg(a) < v);               /* Will borrow */
  reg(a) -= v;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 1); /* Add = 0/Subtract = 1 */
  return 4;
}

static u8 and8(z80e* z80, u8 v) {
  u8 tmp = reg(a);
  reg(a) = reg(a) & v;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_hf(z80, 1);
  set_pof(z80, reg(a) < tmp || reg(a) < v); /* Is overflowed */
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 0);
  set_cf(z80, 0);
  return 4;
}

static u8 xor8(z80e* z80, u8 v) {
  reg(a) = reg(a) ^ v;
  set_sf(z80, (reg(a) & 0x80) != 0); /* Is negative */
  set_zf(z80, reg(a) == 0);
  set_hf(z80, 0);
  set_pof(z80, is_even_parity(reg(a)));
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 0);
  set_cf(z80, 0);
  return 4;
}

static u16 add16(z80e* z80, u16 a, u16 b) {
  u16 res = a + b;
  set_yf(z80, bit(res, 13));
  set_hf(z80, u16_byte_carry(a, b));
  set_xf(z80, bit(res, 11));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
  set_cf(z80, u16_overflow(a, b));
  return res;
}

static u8 is_even_parity(u8 v) {
  u8 n = 0;
  for (u8 i = 0; i < 0x09; ++i) {
    n += ((v & (1 << i)) != 0);
  }
  return n % 2 == 0;
}

static u8 jr(z80e* z80, u8 cond) {
  if (cond) {
    z80->reg.pc += read_byte(z80);
    return 12;
  }
  z80->reg.pc += 1;
  return 7;
}

static void daa(z80e* z80) {
  u8 low = low_nibble(reg(a));
  u8 corr = 0;

  if (low > 0x9 || hf(z80)) {
    corr += 0x06;
  }

  if (reg(a) > 0x99 || cf(z80)) {
    corr += 0x60;
    /*
     * The CF flag is affected as follows:
     *
     *   CF  | high | low | CF'
     *    0  | 0-9  | 0-9 | 0
     *    0  | 0-8  | a-f | 0
     *    0  | 9-f  | a-f | 1
     *    0  | a-f  | 0-9 | 1
     *    1  |  *   |  *  | 1
     *
     * From The Undocumented Z80 Documented v0.91
     *
     * Here:
     *   cf = ((high > 0x8 and low > 0x9) or (high > 0x9 and low < 0xa))
     * Both ranges are greater than 0x99.
     */
    set_cf(z80, 1);
  }

  /*
   * The HF flag is affected as follows:
   *
   *   NF | HF | low | HF'
   *    0 | *  | 0-9 | 0
   *    0 | *  | a-f | 1
   *    1 | 0  |  *  | 0
   *    1 | 1  | 6-f | 0
   *    1 | 1  | 0-5 | 1
   *
   * From The Undocumented Z80 Documented v0.91
   */
  if (nf(z80)) {
    reg(a) -= corr;
    set_hf(z80, hf(z80) && low < 0x6);
  } else {
    reg(a) += corr;
    set_hf(z80, low > 0x9);
  }

  set_sf(z80, bit(reg(a), 7));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_pof(z80, is_even_parity(reg(a)));
}

static void set_bc(z80e* z80, u16 val) {
  reg(b) = val >> 8;
  reg(c) = val;
}

static void set_hl(z80e* z80, u16 val) {
  reg(h) = val >> 8;
  reg(l) = val;
}

static void set_de(z80e* z80, u16 val) {
  reg(d) = val >> 8;
  reg(e) = val;
}

static u8 read_byte(z80e* z80) {
  u8 b = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  return b;
}

static u8 read_byte_at(z80e* z80, u16 addr) { return z80->memread(addr, z80->ctx); }

static u16 read_word(z80e* z80) {
  u8 lsb = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  u8 msb = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  return msb << 8 | lsb;
}

static u16 read_word_at(z80e* z80, u16 addr) { return (read_byte_at(z80, addr) << 8) | read_byte_at(z80, addr + 1); }

static void write_byte(z80e* z80, u8 byte) {
  z80->memwrite(z80->reg.pc, byte, z80->ctx);
  z80->reg.pc += 1;
}

static void write_byte_at(z80e* z80, u8 byte, u16 addr) { z80->memwrite(addr, byte, z80->ctx); }

static void write_word(z80e* z80, u16 word) {
  write_byte(z80, (word >> 8));
  write_byte(z80, word);
}
