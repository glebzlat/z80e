#include "z80e.h"

#define MAXU8 ((u8) - 1)
#define MAXI8 ((i8) - 129)
#define MINI8 ((i8)128)

#define reg(name) (z80->reg.cur->name)

#define bit(v, n) (((v) & (1 << n)) != 0)
#define low_nibble(v) (v & 0x0f)
#define high_nibble(v) (v >> 4)
#define lsb(v) (v & 0xff)
#define msb(v) (v >> 8)

static zi8 z80e_execute(z80e* z80, zu8 opcode);

static void dec8(z80e* z80, zu8* reg);
static void inc8(z80e* z80, zu8* reg);
static void add8(z80e* z80, zu8 v, zu8 c);
static void sub8(z80e* z80, zu8 v, zu8 c);
static void and8(z80e* z80, zu8 v);
static void or8(z80e* z80, zu8 v);
static void xor8(z80e* z80, zu8 v);
static void cp8(z80e* z80, zu8 v);
static void inc_addr(z80e* z80, zu16 addr);
static void dec_addr(z80e* z80, zu16 addr);

static zu16 add16(z80e* z80, zu16 a, zu16 b);
static zu16 inc16(z80e* z80, zu16 v);
static zu16 dec16(z80e* z80, zu16 v);

static void jp(z80e* z80, zu8 cond);

static void push(z80e* z80, zu16 rr);
static zu16 pop(z80e* z80);

static zu8 ldi(z80e* z80);
static zu8 ldir(z80e* z80);
static zu8 ldd(z80e* z80);
static zu8 lddr(z80e* z80);
static zu8 cpi(z80e* z80);
static zu8 cpir(z80e* z80);
static zu8 cpd(z80e* z80);
static zu8 cpdr(z80e* z80);

static zu8 jr(z80e* z80, zu8 cond);
static void daa(z80e* z80);
static void cpl(z80e* z80);
static void neg(z80e* z80);
static void ccf(z80e* z80);
static void scf(z80e* z80);

static void rlc(z80e* z80, zu8* r);
static void rl(z80e* z80, zu8* r);
static void rrc(z80e* z80, zu8* r);
static void rr(z80e* z80, zu8* r);
static void sla(z80e* z80, zu8* r);
static void sra(z80e* z80, zu8* r);
static void srl(z80e* z80, zu8* r);

zu8 call(z80e* z80, zu8 cc, zu16 nn);
zu8 ret(z80e* z80, zu8 cc);

static void bit_op(z80e* z80, zu8 opcode, zu8 r);
static void undoc_bit_hl(z80e* z80, zu8 opcode);
static void set_op(z80e* z80, zu8 opcode, zu8* r);
static void res_op(z80e* z80, zu8 opcode, zu8* r);

static zu8 is_even_parity(zu8 v);

/* Sign flag */
static inline zu8 sf(z80e* z80) { return (reg(f) & (1 << 7)); }
/* Zero flag */
static inline zu8 zf(z80e* z80) { return (reg(f) & (1 << 6)); }
/* Y flag - a copy of bit 5 of the result */
static inline zu8 yf(z80e* z80) { return (reg(f) & (1 << 5)); }
/* Half-carry flag */
static inline zu8 hf(z80e* z80) { return (reg(f) & (1 << 4)); }
/* X flag - a copy of bit 3 of the result */
static inline zu8 xf(z80e* z80) { return (reg(f) & (1 << 3)); }
/* Parity/Overflow flag */
static inline zu8 pof(z80e* z80) { return (reg(f) & (1 << 2)); }
/* Add/Subtract flag */
static inline zu8 nf(z80e* z80) { return (reg(f) & (1 << 1)); }
/* Carry flag */
static inline zu8 cf(z80e* z80) { return (reg(f) & (1 << 0)); }

static inline void set_f(z80e* z80, zu8 v, zu8 pos) {
  if (v) {
    reg(f) |= 1 << pos;
  } else {
    reg(f) &= (1 << pos) ^ 0xff;
  }
}

static inline void set_sf(z80e* z80, zu8 v) { set_f(z80, v, 7); }
static inline void set_zf(z80e* z80, zu8 v) { set_f(z80, v, 6); }
static inline void set_yf(z80e* z80, zu8 v) { set_f(z80, v, 5); }
static inline void set_hf(z80e* z80, zu8 v) { set_f(z80, v, 4); }
static inline void set_xf(z80e* z80, zu8 v) { set_f(z80, v, 3); }
static inline void set_pof(z80e* z80, zu8 v) { set_f(z80, v, 2); }
static inline void set_nf(z80e* z80, zu8 v) { set_f(z80, v, 1); }
static inline void set_cf(z80e* z80, zu8 v) { set_f(z80, v, 0); }

static inline void set_iff(z80e* z80, zu8 v, zu8 i) {
  if (v) {
    z80->iff |= 1 << i;
  } else {
    z80->iff &= (1 << i) ^ 0xff;
  }
}

static inline void set_iff1(z80e* z80, zu8 v) { set_iff(z80, v, 0); }
static inline void set_iff2(z80e* z80, zu8 v) { set_iff(z80, v, 1); }
static inline zu8 iff1(z80e* z80) { return bit(z80->iff, 0); }
static inline zu8 iff2(z80e* z80) { return bit(z80->iff, 1); }

static inline void set_yf_u8(z80e* z80, zu8 v) { set_yf(z80, bit(v, 5)); }
static inline void set_xf_u8(z80e* z80, zu8 v) { set_xf(z80, bit(v, 3)); }
static inline void set_yf_u16(z80e* z80, zu16 v) { set_yf(z80, bit(v, 13)); }
static inline void set_xf_u16(z80e* z80, zu16 v) { set_xf(z80, bit(v, 11)); }

/* Check whether the a + b + c will cause bit carry to i-th bit */
inline static zu8 carry(int i, zu16 a, zu16 b, zu8 c) {
  zu16 mask = (1u << i) - 1;
  zu32 res = (a & mask) + (b & mask) + c;
  return (res >> i) != 0;
}

/* Check whether a - (b + c) will cause bit borrow from i-th bit */
inline static int borrow(int i, zu16 a, zu16 b, zu8 c) {
  zu16 mask = (1u << i) - 1;
  return (a & mask) < ((b + c) & mask);
}

inline static int u8_overflow(zu8 a, zu8 b) { return carry(7, a, b, 0); }
inline static int u8_half_carry(zu8 a, zu8 b) { return carry(4, a, b, 0); }
inline static int u8_half_borrow(zu8 a, zu8 b) { return borrow(4, a, b, 0); }
inline static int u8_negative(zu8 i) { return bit(i, 7); }
inline static int u16_byte_carry(zu16 a, zu16 b) { return carry(8, a, b, 0); }

static zu8 read_byte(z80e* z80);
static zu8 read_byte_at(z80e* z80, zu16 addr);
static zu16 read_word(z80e* z80);
static zu16 read_word_at(z80e* z80, zu16 addr);
static void write_byte(z80e* z80, zu8 byte);
static void write_byte_at(z80e* z80, zu8 byte, zu16 addr);
static void write_word(z80e* z80, zu16 word);
static void write_word_at(z80e* z80, zu16 word, zu16 addr);

static inline zu16 bc(z80e* z80) { return (reg(b) << 8) | reg(c); }
static inline zu16 hl(z80e* z80) { return (reg(h) << 8) | reg(l); }
static inline zu16 de(z80e* z80) { return (reg(d) << 8) | reg(e); }
static inline zu16 sp(z80e* z80) { return z80->reg.sp; }
static inline zu16 af(z80e* z80) { return (reg(a) << 8) | reg(f); }

static void set_bc(z80e* z80, zu16 val);
static void set_hl(z80e* z80, zu16 val);
static void set_de(z80e* z80, zu16 val);
static void set_sp(z80e* z80, zu16 val);
static void set_af(z80e* z80, zu16 val);

static zi8 z80e_execute_cb(z80e* z80, zu8 opcode);
static zi8 z80e_execute_ed(z80e* z80, zu8 opcode);
static zi8 z80e_execute_ddfd(z80e* z80, zu16* rr, zu8 opcode);

void z80e_init(z80e* z80, z80e_config* config) {
  for (zu32 i = 0; i < sizeof(*z80); ++i) {
    ((zu8*)z80)[i] = 0;
  }

  z80->memread = config->memread;
  z80->memwrite = config->memwrite;
  z80->ioread = config->ioread;
  z80->iowrite = config->iowrite;
  z80->ctx = config->ctx;

  z80->reg.cur = &z80->reg.main;
}

zi8 z80e_instruction(z80e* z80) {
  if (z80->error) {
    return z80->error;
  }

  if (z80->halt) {
    return 4;
  }

  zu8 opcode = read_byte(z80);
  zi8 ret = z80e_execute(z80, opcode);
  if (ret < 0) {
    z80->error = ret;
  }

  return ret;
}

void z80e_halt(z80e* z80) { z80->halt = 1; }

int z80e_get_halt(z80e* z80) { return z80->halt; }

static zi8 z80e_execute(z80e* z80, zu8 opcode) {
  zu8 tmp8;
  zu16 tmp16;

  switch (opcode) {
    /* clang-format off */
  case 0x78: reg(a) = reg(b); return 4; /* ld a, b */
  case 0x79: reg(a) = reg(c); return 4; /* ld a, c */
  case 0x7a: reg(a) = reg(d); return 4; /* ld a, d */
  case 0x7b: reg(a) = reg(e); return 4; /* ld a, e */
  case 0x7c: reg(a) = reg(h); return 4; /* ld a, h */
  case 0x7d: reg(a) = reg(l); return 4; /* ld a, l */
  case 0x7f: reg(a) = reg(a); return 4; /* ld a, a */

  case 0x40: reg(b) = reg(b); return 4; /* ld b, b */
  case 0x41: reg(b) = reg(c); return 4; /* ld b, c */
  case 0x42: reg(b) = reg(d); return 4; /* ld b, d */
  case 0x43: reg(b) = reg(e); return 4; /* ld b, e */
  case 0x44: reg(b) = reg(h); return 4; /* ld b, h */
  case 0x45: reg(b) = reg(l); return 4; /* ld b, l */
  case 0x47: reg(b) = reg(a); return 4; /* ld b, a */

  case 0x48: reg(c) = reg(b); return 4; /* ld c, b */
  case 0x49: reg(c) = reg(c); return 4; /* ld c, c */
  case 0x4a: reg(c) = reg(d); return 4; /* ld c, d */
  case 0x4b: reg(c) = reg(e); return 4; /* ld c, e */
  case 0x4c: reg(c) = reg(h); return 4; /* ld c, h */
  case 0x4d: reg(c) = reg(l); return 4; /* ld c, l */
  case 0x4f: reg(c) = reg(a); return 4; /* ld c, a */

  case 0x50: reg(d) = reg(b); return 4; /* ld d, b */
  case 0x51: reg(d) = reg(c); return 4; /* ld d, c */
  case 0x52: reg(d) = reg(d); return 4; /* ld d, d */
  case 0x53: reg(d) = reg(e); return 4; /* ld d, e */
  case 0x54: reg(d) = reg(h); return 4; /* ld d, h */
  case 0x55: reg(d) = reg(l); return 4; /* ld d, l */
  case 0x57: reg(d) = reg(a); return 4; /* ld d, a */

  case 0x58: reg(e) = reg(b); return 4; /* ld e, b */
  case 0x59: reg(e) = reg(c); return 4; /* ld e, c */
  case 0x5a: reg(e) = reg(d); return 4; /* ld e, d */
  case 0x5b: reg(e) = reg(e); return 4; /* ld e, e */
  case 0x5c: reg(e) = reg(h); return 4; /* ld e, h */
  case 0x5d: reg(e) = reg(l); return 4; /* ld e, l */
  case 0x5f: reg(e) = reg(a); return 4; /* ld e, a */

  case 0x60: reg(h) = reg(b); return 4; /* ld h, b */
  case 0x61: reg(h) = reg(c); return 4; /* ld h, c */
  case 0x62: reg(h) = reg(d); return 4; /* ld h, d */
  case 0x63: reg(h) = reg(e); return 4; /* ld h, e */
  case 0x64: reg(h) = reg(h); return 4; /* ld h, h */
  case 0x65: reg(h) = reg(l); return 4; /* ld h, l */
  case 0x67: reg(h) = reg(a); return 4; /* ld h, a */

  case 0x68: reg(l) = reg(b); return 4; /* ld l, b */
  case 0x69: reg(l) = reg(c); return 4; /* ld l, c */
  case 0x6a: reg(l) = reg(d); return 4; /* ld l, d */
  case 0x6b: reg(l) = reg(e); return 4; /* ld l, e */
  case 0x6c: reg(l) = reg(h); return 4; /* ld l, h */
  case 0x6d: reg(l) = reg(l); return 4; /* ld l, l */
  case 0x6f: reg(l) = reg(a); return 4; /* ld l, a */

  case 0x3e: reg(a) = read_byte(z80); return 7; /* ld a, n */
  case 0x06: reg(b) = read_byte(z80); return 7; /* ld b, n */
  case 0x0e: reg(c) = read_byte(z80); return 7; /* ld c, n */
  case 0x16: reg(d) = read_byte(z80); return 7; /* ld d, n */
  case 0x1e: reg(e) = read_byte(z80); return 7; /* ld e, n */
  case 0x26: reg(h) = read_byte(z80); return 7; /* ld h, n */
  case 0x2e: reg(l) = read_byte(z80); return 7; /* ld l, n */

  case 0x7e: reg(a) = read_byte_at(z80, hl(z80)); return 7; /* ld a, (hl) */
  case 0x46: reg(b) = read_byte_at(z80, hl(z80)); return 7; /* ld b, (hl) */
  case 0x4e: reg(c) = read_byte_at(z80, hl(z80)); return 7; /* ld c, (hl) */
  case 0x56: reg(d) = read_byte_at(z80, hl(z80)); return 7; /* ld d, (hl) */
  case 0x5e: reg(e) = read_byte_at(z80, hl(z80)); return 6; /* ld e, (hl) */
  case 0x66: reg(h) = read_byte_at(z80, hl(z80)); return 7; /* ld h, (hl) */
  case 0x6e: reg(l) = read_byte_at(z80, hl(z80)); return 6; /* ld l, (hl) */

  case 0x70: write_byte_at(z80, reg(b), hl(z80)); return 7; /* ld (hl), b */
  case 0x71: write_byte_at(z80, reg(c), hl(z80)); return 7; /* ld (hl), c */
  case 0x72: write_byte_at(z80, reg(d), hl(z80)); return 7; /* ld (hl), d */
  case 0x73: write_byte_at(z80, reg(e), hl(z80)); return 7; /* ld (hl), e */
  case 0x74: write_byte_at(z80, reg(h), hl(z80)); return 7; /* ld (hl), h */
  case 0x75: write_byte_at(z80, reg(l), hl(z80)); return 7; /* ld (hl), l */
  case 0x77: write_byte_at(z80, reg(a), hl(z80)); return 7; /* ld (hl), a */
  case 0x36: write_byte_at(z80, read_byte(z80), hl(z80)); return 10; /* ld (hl), n */

  case 0x0a: reg(a) = read_byte_at(z80, bc(z80)); return 7; /* ld a, (bc) */
  case 0x1a: reg(a) = read_byte_at(z80, de(z80)); return 7; /* ld a, (de) */
  case 0x3a: reg(a) = read_byte_at(z80, read_word(z80)); return 13; /* ld a, (nn) */

  case 0x02: write_byte_at(z80, reg(a), bc(z80)); return 7; /* ld (bc), a */
  case 0x12: write_byte_at(z80, reg(a), de(z80)); return 7; /* ld (de), a */
  case 0x32: write_byte_at(z80, reg(a), read_word(z80)); return 13; /* ld (nn), a */

  case 0x01: set_bc(z80, read_word(z80)); return 10; /* ld bc, nn */
  case 0x11: set_de(z80, read_word(z80)); return 10; /* ld de, nn */
  case 0x21: set_hl(z80, read_word(z80)); return 10; /* ld hl, nn */
  case 0x31: z80->reg.sp = read_word(z80); return 10; /* ld sp, nn */

  case 0x2a: set_hl(z80, read_word_at(z80, read_word(z80))); return 16; /* ld hl, (nn) */
  case 0x22: write_word(z80, hl(z80)); return 16; /* ld (nn), hl */
  case 0xf9: set_sp(z80, hl(z80)); return 6; /* ld sp, hl */
  case 0xc5: push(z80, bc(z80)); return 11; /* push bc */
  case 0xd5: push(z80, de(z80)); return 11; /* push de */
  case 0xe5: push(z80, hl(z80)); return 11; /* push hl */
  case 0xf5: push(z80, af(z80)); return 11; /* push af */
  case 0xc1: set_bc(z80, pop(z80)); return 10; /* pop bc */
  case 0xd1: set_de(z80, pop(z80)); return 10; /* pop de */
  case 0xe1: set_hl(z80, pop(z80)); return 10; /* pop hl */
  case 0xf1: set_af(z80, pop(z80)); return 10; /* pop af */

  case 0x04: inc8(z80, &reg(b)); return 4; /* inc b */
  case 0x0c: inc8(z80, &reg(c)); return 4; /* inc c */
  case 0x14: inc8(z80, &reg(d)); return 4; /* inc d */
  case 0x1c: inc8(z80, &reg(e)); return 4; /* inc e */
  case 0x24: inc8(z80, &reg(h)); return 4; /* inc h */
  case 0x2c: inc8(z80, &reg(l)); return 4; /* inc l */
  case 0x3c: inc8(z80, &reg(a)); return 4; /* inc a */

  case 0x05: dec8(z80, &reg(b)); return 4; /* dec b */
  case 0x0d: dec8(z80, &reg(c)); return 4; /* dec c */
  case 0x15: dec8(z80, &reg(d)); return 4; /* dec d */
  case 0x1d: dec8(z80, &reg(e)); return 4; /* dec e */
  case 0x25: dec8(z80, &reg(h)); return 4; /* dec h */
  case 0x2d: dec8(z80, &reg(l)); return 4; /* dec l */
  case 0x3d: dec8(z80, &reg(a)); return 4; /* dec a */

  case 0x80: add8(z80, reg(b), 0); return 4; /* add a, b */
  case 0x81: add8(z80, reg(c), 0); return 4; /* add a, c */
  case 0x82: add8(z80, reg(d), 0); return 4; /* add a, d */
  case 0x83: add8(z80, reg(e), 0); return 4; /* add a, e */
  case 0x84: add8(z80, reg(h), 0); return 4; /* add a, h */
  case 0x85: add8(z80, reg(l), 0); return 4; /* add a, l */
  case 0x87: add8(z80, reg(a), 0); return 4; /* add a, a */
  case 0xc6: add8(z80, read_byte(z80), 0); return 7; /* add a, n */
  case 0x86: add8(z80, read_byte_at(z80, hl(z80)), 0); return 7; /* add a, (hl) */

  case 0x88: add8(z80, reg(b), cf(z80)); return 4; /* adc a, b */
  case 0x89: add8(z80, reg(c), cf(z80)); return 4; /* adc a, c */
  case 0x8a: add8(z80, reg(d), cf(z80)); return 4; /* adc a, d */
  case 0x8b: add8(z80, reg(e), cf(z80)); return 4; /* adc a, e */
  case 0x8c: add8(z80, reg(h), cf(z80)); return 4; /* adc a, h */
  case 0x8d: add8(z80, reg(l), cf(z80)); return 4; /* adc a, l */
  case 0x8f: add8(z80, reg(a), cf(z80)); return 4; /* adc a, a */
  case 0xce: add8(z80, read_byte(z80), cf(z80)); return 7; /* adc a, n */
  case 0x8e: add8(z80, read_byte_at(z80, hl(z80)), cf(z80)); return 7; /* adc a, (hl) */

  case 0x90: sub8(z80, reg(b), 0); return 4; /* sub b */
  case 0x91: sub8(z80, reg(c), 0); return 4; /* sub c */
  case 0x92: sub8(z80, reg(d), 0); return 4; /* sub d */
  case 0x93: sub8(z80, reg(e), 0); return 4; /* sub e */
  case 0x94: sub8(z80, reg(h), 0); return 4; /* sub h */
  case 0x95: sub8(z80, reg(l), 0); return 4; /* sub l */
  case 0x97: sub8(z80, reg(a), 0); return 4; /* sub a */
  case 0xd6: sub8(z80, read_byte(z80), 0); return 7; /* sub n */
  case 0x96: sub8(z80, read_byte_at(z80, hl(z80)), 0); return 7; /* sub (hl) */

  case 0x98: sub8(z80, reg(b), cf(z80)); return 4; /* sbc a, b */
  case 0x99: sub8(z80, reg(c), cf(z80)); return 4; /* sbc a, c */
  case 0x9a: sub8(z80, reg(d), cf(z80)); return 4; /* sbc a, d */
  case 0x9b: sub8(z80, reg(e), cf(z80)); return 4; /* sbc a, e */
  case 0x9c: sub8(z80, reg(h), cf(z80)); return 4; /* sbc a, h */
  case 0x9d: sub8(z80, reg(l), cf(z80)); return 4; /* sbc a, l */
  case 0x9f: sub8(z80, reg(a), cf(z80)); return 4; /* sbc a, a */
  case 0xde: sub8(z80, read_byte(z80), cf(z80)); return 7; /* sbc a, n */
  case 0x9e: sub8(z80, read_byte_at(z80, hl(z80)), cf(z80)); return 7; /* sbc a, (hl) */

  case 0xa0: and8(z80, reg(b)); return 4; /* and b */
  case 0xa1: and8(z80, reg(c)); return 4; /* and c */
  case 0xa2: and8(z80, reg(d)); return 4; /* and d */
  case 0xa3: and8(z80, reg(e)); return 4; /* and e */
  case 0xa4: and8(z80, reg(h)); return 4; /* and h */
  case 0xa5: and8(z80, reg(l)); return 4; /* and l */
  case 0xa7: and8(z80, reg(a)); return 4; /* and a */
  case 0xe6: and8(z80, read_byte(z80)); return 7; /* and n */
  case 0xa6: and8(z80, read_byte_at(z80, hl(z80))); return 7; /* and (hl) */

  case 0xb0: or8(z80, reg(b)); return 4; /* or b */
  case 0xb1: or8(z80, reg(c)); return 4; /* or c */
  case 0xb2: or8(z80, reg(d)); return 4; /* or d */
  case 0xb3: or8(z80, reg(e)); return 4; /* or e */
  case 0xb4: or8(z80, reg(h)); return 4; /* or h */
  case 0xb5: or8(z80, reg(l)); return 4; /* or l */
  case 0xb7: or8(z80, reg(a)); return 4; /* or a */
  case 0xf6: or8(z80, read_byte(z80)); return 7; /* or n */
  case 0xb6: or8(z80, read_byte_at(z80, hl(z80))); return 7; /* or (hl) */

  case 0xa8: xor8(z80, reg(b)); return 4; /* xor b */
  case 0xa9: xor8(z80, reg(c)); return 4; /* xor c */
  case 0xaa: xor8(z80, reg(d)); return 4; /* xor d */
  case 0xab: xor8(z80, reg(e)); return 4; /* xor e */
  case 0xac: xor8(z80, reg(h)); return 4; /* xor h */
  case 0xad: xor8(z80, reg(l)); return 4; /* xor l */
  case 0xaf: xor8(z80, reg(a)); return 4; /* xor a */
  case 0xee: xor8(z80, read_byte(z80)); return 7; /* xor n */
  case 0xae: xor8(z80, read_byte_at(z80, hl(z80))); return 7; /* xor (hl) */

  case 0xb8: cp8(z80, reg(b)); return 4; /* cp b */
  case 0xb9: cp8(z80, reg(c)); return 4; /* cp c */
  case 0xba: cp8(z80, reg(d)); return 4; /* cp d */
  case 0xbb: cp8(z80, reg(e)); return 4; /* cp e */
  case 0xbc: cp8(z80, reg(h)); return 4; /* cp h */
  case 0xbd: cp8(z80, reg(l)); return 4; /* cp l */
  case 0xbf: cp8(z80, reg(a)); return 4; /* cp a */
  case 0xfe: cp8(z80, read_byte(z80)); return 7; /* cp n */
  case 0xbe: cp8(z80, read_byte_at(z80, hl(z80))); return 7; /* cp (hl) */

  case 0x29: set_hl(z80, add16(z80, hl(z80), hl(z80))); return 11; /* add hl, hl */
  case 0x39: set_hl(z80, add16(z80, hl(z80), z80->reg.sp)); return 11; /* add hl, sp */
  case 0x19: set_hl(z80, add16(z80, hl(z80), de(z80))); return 11; /* add hl, de */
  case 0x09: set_hl(z80, add16(z80, hl(z80), bc(z80))); return 11; /* add hl, bc */

  case 0x03: set_bc(z80, inc16(z80, bc(z80))); return 6; /* inc bc */
  case 0x13: set_de(z80, inc16(z80, de(z80))); return 6; /* inc de */
  case 0x23: set_hl(z80, inc16(z80, hl(z80))); return 6; /* inc hl */
  case 0x33: z80->reg.sp = inc16(z80, z80->reg.sp); return 6; /* inc sp */

  case 0x0b: set_bc(z80, dec16(z80, bc(z80))); return 6; /* dec bc */
  case 0x1b: set_de(z80, dec16(z80, de(z80))); return 6; /* dec de */
  case 0x2b: set_hl(z80, dec16(z80, hl(z80))); return 6; /* dec hl */
  case 0x3b: z80->reg.sp = inc16(z80, z80->reg.sp); return 6; /* dec sp */

  case 0xc3: jp(z80, 1); return 10; /* jp nn */
  case 0xc2: jp(z80, !zf(z80)); return 10; /* jp nz, nn */
  case 0xca: jp(z80, zf(z80)); return 10; /* jp z, nn */
  case 0xd2: jp(z80, !cf(z80)); return 10; /* jp nc, nn */
  case 0xda: jp(z80, cf(z80)); return 10; /* jp c, nn */
  case 0xe2: jp(z80, !pof(z80)); return 10; /* jp po, nn */
  case 0xea: jp(z80, pof(z80)); return 10; /* jp pe, nn */
  case 0xf2: jp(z80, !sf(z80)); return 10; /* jp p, nn */
  case 0xfa: jp(z80, sf(z80)); return 10; /* jp m, nn */
  case 0xe9: z80->reg.pc = hl(z80); return 4; /* jp (hl) */
  case 0x18: return jr(z80, 1); /* jr d */
  case 0x28: return jr(z80, zf(z80)); /* jr z, d */
  case 0x20: return jr(z80, !zf(z80)); /* jr nz, d */
  case 0x38: return jr(z80, cf(z80)); /* jr c, d */
  case 0x30: return jr(z80, !cf(z80)); /* jr nc, d */
  case 0x10: reg(b) = reg(b) - 1; return jr(z80, reg(b) != 0) + 1; /* djnz d */

  case 0x27: daa(z80); return 4; /* daa */
  case 0x2f: cpl(z80); return 4; /* cpl */

  case 0x3f: ccf(z80); return 4; /* ccf */
  case 0x37: scf(z80); return 4; /* scf */
  case 0x00: return 4; /* nop */
  case 0x76: z80->halt = 1; return 4; /* halt */
  case 0xf3: set_iff1(z80, 0); set_iff2(z80, 0); return 4; /* di */
  case 0xfb: set_iff1(z80, 1); set_iff2(z80, 1); return 4; /* ei */

    /* clang-format on */

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
    set_yf(z80, bit(reg(a), 5));
    set_xf(z80, bit(reg(a), 3));
    return 4;

  case 0xeb: /* ex de, hl */
    tmp16 = de(z80);
    set_de(z80, hl(z80));
    set_hl(z80, tmp16);
    return 4;

  case 0x08: /* ex af, af' */
    tmp8 = z80->reg.main.a;
    z80->reg.alt.a = z80->reg.main.a;
    z80->reg.main.a = tmp8;
    tmp8 = z80->reg.main.f;
    z80->reg.alt.f = z80->reg.main.f;
    z80->reg.main.f = tmp8;
    return 4;

  case 0xd9: /* exx */
    if (z80->reg.cur == &z80->reg.main) {
      z80->reg.cur = &z80->reg.alt;
    } else if (z80->reg.cur == &z80->reg.alt) {
      z80->reg.cur = &z80->reg.main;
    }
    return 4;

  case 0xe3: /* ex (sp), hl */
    tmp16 = read_word_at(z80, z80->reg.sp);
    write_word_at(z80, hl(z80), z80->reg.sp);
    set_hl(z80, tmp16);
    return 4;

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
    dec8(z80, &tmp8);
    write_byte_at(z80, tmp8, hl(z80));
    return 11;

  case 0xcd: return call(z80, 1, read_word(z80));         /* call nn */
  case 0xc4: return call(z80, !zf(z80), read_word(z80));  /* call nz, nn */
  case 0xcc: return call(z80, zf(z80), read_word(z80));   /* call z, nn */
  case 0xd4: return call(z80, !cf(z80), read_word(z80));  /* call nc, nn */
  case 0xdc: return call(z80, cf(z80), read_word(z80));   /* call c, nn */
  case 0xe4: return call(z80, !pof(z80), read_word(z80)); /* call po, nn */
  case 0xec: return call(z80, pof(z80), read_word(z80));  /* call pe, nn */
  case 0xf4: return call(z80, !sf(z80), read_word(z80));  /* call p, nn */
  case 0xfc: return call(z80, sf(z80), read_word(z80));   /* call m, nn */

  case 0xc9: return ret(z80, 1);         /* ret */
  case 0xc0: return ret(z80, !zf(z80));  /* ret nz */
  case 0xc8: return ret(z80, zf(z80));   /* ret z */
  case 0xd0: return ret(z80, !cf(z80));  /* ret nc */
  case 0xd8: return ret(z80, cf(z80));   /* ret c */
  case 0xe0: return ret(z80, !pof(z80)); /* ret po */
  case 0xe8: return ret(z80, pof(z80));  /* ret pe */
  case 0xf0: return ret(z80, !sf(z80));  /* ret p */
  case 0xf8: return ret(z80, sf(z80));   /* ret m */

  case 0xc7: return call(z80, 1, 0x0000); /* rst 0x00 */
  case 0xcf: return call(z80, 1, 0x0008); /* rst 0x08 */
  case 0xd7: return call(z80, 1, 0x0010); /* rst 0x10 */
  case 0xdf: return call(z80, 1, 0x0018); /* rst 0x18 */
  case 0xe7: return call(z80, 1, 0x0020); /* rst 0x20 */
  case 0xef: return call(z80, 1, 0x0028); /* rst 0x28 */
  case 0xf7: return call(z80, 1, 0x0030); /* rst 0x30 */
  case 0xff: return call(z80, 1, 0x0038); /* rst 0x38 */

  case 0xcb:
    return z80e_execute_cb(z80, read_byte(z80));

  case 0xed:
    return z80e_execute_ed(z80, read_byte(z80));

  case 0xdd:
    return z80e_execute_ddfd(z80, &z80->reg.ix, read_byte(z80));

  case 0xfd:
    return z80e_execute_ddfd(z80, &z80->reg.iy, read_byte(z80));

  default:
    return Z80E_INVALID_OPCODE;
  };
}

static zi8 z80e_execute_cb(z80e* z80, zu8 opcode) {
  zu8 tmp;

#define op_hl_pointer(fn)                                                                                              \
  tmp = read_byte_at(z80, hl(z80));                                                                                    \
  fn;                                                                                                                  \
  write_byte_at(z80, tmp, hl(z80));

#define encode_bit_index(bit_pattern, code)                                                                            \
  case 0x00 | bit_pattern:                                                                                             \
  case 0x08 | bit_pattern:                                                                                             \
  case 0x10 | bit_pattern:                                                                                             \
  case 0x18 | bit_pattern:                                                                                             \
  case 0x20 | bit_pattern:                                                                                             \
  case 0x28 | bit_pattern:                                                                                             \
  case 0x30 | bit_pattern:                                                                                             \
  case 0x38 | bit_pattern:                                                                                             \
    code;

  switch (opcode) {
    /* clang-format off */
  case 0x07: rlc(z80, &reg(a)); return 8; /* rlc a */
  case 0x00: rlc(z80, &reg(b)); return 8; /* rlc b */
  case 0x01: rlc(z80, &reg(c)); return 8; /* rlc c */
  case 0x02: rlc(z80, &reg(d)); return 8; /* rlc d */
  case 0x03: rlc(z80, &reg(e)); return 8; /* rlc e */
  case 0x04: rlc(z80, &reg(h)); return 8; /* rlc h */
  case 0x05: rlc(z80, &reg(l)); return 8; /* rlc l */
  case 0x06: op_hl_pointer(rlc(z80, &tmp)); return 15; /* rlc (hl) */

  case 0x17: rl(z80, &reg(a)); return 8; /* rl a */
  case 0x10: rl(z80, &reg(b)); return 8; /* rl b */
  case 0x11: rl(z80, &reg(c)); return 8; /* rl c */
  case 0x12: rl(z80, &reg(d)); return 8; /* rl d */
  case 0x13: rl(z80, &reg(e)); return 8; /* rl e */
  case 0x14: rl(z80, &reg(h)); return 8; /* rl h */
  case 0x15: rl(z80, &reg(l)); return 8; /* rl l */
  case 0x16: op_hl_pointer(rl(z80, &tmp)); return 15; /* rl (hl) */

  case 0x0f: rrc(z80, &reg(a)); return 8; /* rrc a */
  case 0x08: rrc(z80, &reg(b)); return 8; /* rrc b */
  case 0x09: rrc(z80, &reg(c)); return 8; /* rrc c */
  case 0x0a: rrc(z80, &reg(d)); return 8; /* rrc d */
  case 0x0b: rrc(z80, &reg(e)); return 8; /* rrc e */
  case 0x0c: rrc(z80, &reg(h)); return 8; /* rrc h */
  case 0x0d: rrc(z80, &reg(l)); return 8; /* rrc l */
  case 0x0e: op_hl_pointer(rrc(z80, &tmp)); return 15; /* rrc (hl) */

  case 0x1f: rr(z80, &reg(a)); return 8; /* rr a */
  case 0x18: rr(z80, &reg(b)); return 8; /* rr b */
  case 0x19: rr(z80, &reg(c)); return 8; /* rr c */
  case 0x1a: rr(z80, &reg(d)); return 8; /* rr d */
  case 0x1b: rr(z80, &reg(e)); return 8; /* rr e */
  case 0x1c: rr(z80, &reg(h)); return 8; /* rr h */
  case 0x1d: rr(z80, &reg(l)); return 8; /* rr l */
  case 0x1e: op_hl_pointer(rr(z80, &tmp)); return 15; /* rr (hl) */

  case 0x27: sla(z80, &reg(a)); return 8; /* sla a */
  case 0x20: sla(z80, &reg(b)); return 8; /* sla b */
  case 0x21: sla(z80, &reg(c)); return 8; /* sla c */
  case 0x22: sla(z80, &reg(d)); return 8; /* sla d */
  case 0x23: sla(z80, &reg(e)); return 8; /* sla e */
  case 0x24: sla(z80, &reg(h)); return 8; /* sla h */
  case 0x25: sla(z80, &reg(l)); return 8; /* sla l */
  case 0x26: op_hl_pointer(sla(z80, &tmp)); return 15; /* sla (hl) */

  case 0x2f: sra(z80, &reg(a)); return 8; /* sra a */
  case 0x28: sra(z80, &reg(b)); return 8; /* sra b */
  case 0x29: sra(z80, &reg(c)); return 8; /* sra c */
  case 0x2a: sra(z80, &reg(d)); return 8; /* sra d */
  case 0x2b: sra(z80, &reg(e)); return 8; /* sra e */
  case 0x2c: sra(z80, &reg(h)); return 8; /* sra h */
  case 0x2d: sra(z80, &reg(l)); return 8; /* sra l */
  case 0x2e: op_hl_pointer(sra(z80, &tmp)); return 15; /* sra (hl) */

  case 0x3f: srl(z80, &reg(a)); return 8; /* srl a */
  case 0x38: srl(z80, &reg(b)); return 8; /* srl b */
  case 0x39: srl(z80, &reg(c)); return 8; /* srl c */
  case 0x3a: srl(z80, &reg(d)); return 8; /* srl d */
  case 0x3b: srl(z80, &reg(e)); return 8; /* srl e */
  case 0x3c: srl(z80, &reg(h)); return 8; /* srl h */
  case 0x3d: srl(z80, &reg(l)); return 8; /* srl l */
  case 0x3e: op_hl_pointer(srl(z80, &tmp)); return 15; /* srl (hl) */

  encode_bit_index(0x47, { bit_op(z80, opcode, reg(a)); return 2; }); /* bit n, a */
  encode_bit_index(0x40, { bit_op(z80, opcode, reg(b)); return 2; }); /* bit n, b */
  encode_bit_index(0x41, { bit_op(z80, opcode, reg(c)); return 2; }); /* bit n, c */
  encode_bit_index(0x42, { bit_op(z80, opcode, reg(d)); return 2; }); /* bit n, d */
  encode_bit_index(0x43, { bit_op(z80, opcode, reg(e)); return 2; }); /* bit n, e */
  encode_bit_index(0x44, { bit_op(z80, opcode, reg(h)); return 2; }); /* bit n, h */
  encode_bit_index(0x45, { bit_op(z80, opcode, reg(l)); return 2; }); /* bit n, l */
  encode_bit_index(0x46, { undoc_bit_hl(z80, opcode); return 3; }); /* bit n, (hl) */

  encode_bit_index(0xc7, { set_op(z80, opcode, &reg(a)); return 2; }); /* set n, a */
  encode_bit_index(0xc0, { set_op(z80, opcode, &reg(b)); return 2; }); /* set n, b */
  encode_bit_index(0xc1, { set_op(z80, opcode, &reg(c)); return 2; }); /* set n, c */
  encode_bit_index(0xc2, { set_op(z80, opcode, &reg(d)); return 2; }); /* set n, d */
  encode_bit_index(0xc3, { set_op(z80, opcode, &reg(e)); return 2; }); /* set n, e */
  encode_bit_index(0xc4, { set_op(z80, opcode, &reg(h)); return 2; }); /* set n, h */
  encode_bit_index(0xc5, { set_op(z80, opcode, &reg(l)); return 2; }); /* set n, l */
  encode_bit_index(0xc6, { op_hl_pointer(set_op(z80, opcode, &tmp)); return 4; }); /* set n, (hl) */

  encode_bit_index(0x87, { res_op(z80, opcode, &reg(a)); return 2; }); /* res n, a */
  encode_bit_index(0x80, { res_op(z80, opcode, &reg(b)); return 2; }); /* res n, b */
  encode_bit_index(0x81, { res_op(z80, opcode, &reg(c)); return 2; }); /* res n, c */
  encode_bit_index(0x82, { res_op(z80, opcode, &reg(d)); return 2; }); /* res n, d */
  encode_bit_index(0x83, { res_op(z80, opcode, &reg(e)); return 2; }); /* res n, e */
  encode_bit_index(0x84, { res_op(z80, opcode, &reg(h)); return 2; }); /* res n, h */
  encode_bit_index(0x85, { res_op(z80, opcode, &reg(l)); return 2; }); /* res n, l */
  encode_bit_index(0x86, { op_hl_pointer(res_op(z80, opcode, &tmp)); return 4; }); /* res n, (hl) */

    /* clang-format on */

  default:
    return Z80E_INVALID_OPCODE;
  }

#undef op_hl_pointer
}

static zi8 z80e_execute_ed(z80e* z80, zu8 opcode) {
  switch (opcode) {
    /* clang-format off */
  case 0x47: z80->reg.i = reg(a); return 9; /* ld i, a */
  case 0x4f: z80->reg.r = reg(a); return 9; /* ld r, a */

  case 0x4b: set_bc(z80, read_word_at(z80, read_word(z80))); return 20; /* ld bc, (nn) */
  case 0x5b: set_de(z80, read_word_at(z80, read_word(z80))); return 20; /* ld de, (nn) */
  case 0x6b: set_hl(z80, read_word_at(z80, read_word(z80))); return 20; /* ld hl, (nn) */
  case 0x7b: set_sp(z80, read_word_at(z80, read_word(z80))); return 20; /* ld sp, (nn) */

  case 0x43: write_word_at(z80, bc(z80), read_word(z80)); return 20; /* ld (nn), bc */
  case 0x53: write_word_at(z80, de(z80), read_word(z80)); return 20; /* ld (nn), de */
  case 0x63: write_word_at(z80, hl(z80), read_word(z80)); return 20; /* ld (nn), hl */
  case 0x73: write_word_at(z80, sp(z80), read_word(z80)); return 20; /* ld (nn), sp */

  case 0xa0: return ldi(z80); /* ldi */
  case 0xb0: return ldir(z80); /* ldir */
  case 0xa8: return ldd(z80); /* ldd */
  case 0xb8: return lddr(z80); /* lddr */
  case 0xa1: return cpi(z80); /* cpi */
  case 0xb1: return cpir(z80); /* cpir */
  case 0xa9: return cpd(z80); /* cpd */
  case 0xb9: return cpdr(z80); /* cpdr */

  case 0x44: neg(z80); return 8; return 8; /* neg */
  case 0x46: z80->int_mode = 0; return 8; /* im 0 */
  case 0x56: z80->int_mode = 1; return 8; /* im 1 */
  case 0x5e: z80->int_mode = 2; return 8; /* im 2 */
    /* clang-format on */

  case 0x57: /* ld a, i */
    reg(a) = z80->reg.i;
    set_sf(z80, u8_negative(z80->reg.i));
    set_zf(z80, z80->reg.i == 0);
    set_hf(z80, 0);
    set_pof(z80, iff2(z80));
    set_nf(z80, 0);
    return 9;

  case 0x5f: /* ld a, r */
    reg(a) = z80->reg.r;
    set_sf(z80, u8_negative(z80->reg.r));
    set_zf(z80, z80->reg.r == 0);
    set_hf(z80, 0);
    set_pof(z80, iff2(z80));
    set_nf(z80, 0);
    return 9;

  default:
    return Z80E_INVALID_OPCODE;
  }
}

static zi8 z80e_execute_ddfd(z80e* z80, zu16* rr, zu8 opcode) {
  switch (opcode) {
    /* clang-format off */
  case 0x7e: reg(a) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld a, (iz+d) */
  case 0x46: reg(b) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld b, (iz+d) */
  case 0x4e: reg(c) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld c, (iz+d) */
  case 0x56: reg(d) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld d, (iz+d) */
  case 0x5e: reg(e) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld e, (iz+d) */
  case 0x66: reg(h) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld h, (iz+d) */
  case 0x6e: reg(l) = read_byte_at(z80, *rr + (zi8)read_byte(z80)); return 19; /* ld l, (iz+d) */

  case 0x70: write_byte_at(z80, reg(b), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), b */
  case 0x71: write_byte_at(z80, reg(c), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), c */
  case 0x72: write_byte_at(z80, reg(d), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), d */
  case 0x73: write_byte_at(z80, reg(e), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), e */
  case 0x74: write_byte_at(z80, reg(h), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), h */
  case 0x75: write_byte_at(z80, reg(l), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), l */
  case 0x77: write_byte_at(z80, reg(a), *rr + (zi8)read_byte(z80)); return 19; /* ld (iz+d), a */

  case 0x22: write_word_at(z80, *rr, read_word(z80)); return 20; /* ld (nn), iz */
  case 0xf9: z80->reg.sp = *rr; return 10; /* ld sp, iz */

  case 0x21: *rr = read_word(z80); return 14; /* ld iz, nn */
  case 0x2a: *rr = read_word_at(z80, read_word(z80)); return 20; /* ld iz, nn */

  case 0xe5: push(z80, *rr); return 15; /* push iz */
  case 0xe1: *rr = pop(z80); return 14; /* pop iz */

  case 0x86: add8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80)), 0); return 19; /* add a, (iz+d) */
  case 0x8e: add8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80)), cf(z80)); return 19; /* adc a, (iz+d) */
  case 0x96: sub8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80)), 0); return 19; /* sub a, (iz+d) */
  case 0x9e: sub8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80)), cf(z80)); return 19; /* sbc a, (iz+d) */
  case 0xa6: and8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80))); return 19; /* and (iz+d) */
  case 0xb6: or8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80))); return 19; /* or (iz+d) */
  case 0xae: xor8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80))); return 19; /* xor (iz+d) */
  case 0xbe: cp8(z80, read_byte_at(z80, *rr + (zi8)read_byte(z80))); return 19; /* cp (iz+d) */
  case 0x32: inc_addr(z80, *rr + (zi8)read_byte(z80)); return 23; /* inc (iz+d) */
  case 0x35: dec_addr(z80, *rr + (zi8)read_byte(z80)); return 23; /* dec (iz+d) */
    /* clang-format on */

  case 0x36: /* ld (iz+d), n */
    z80->state.tmp = read_byte(z80);
    write_byte_at(z80, read_byte(z80), *rr + (zi8)z80->state.tmp);
    return 19;

  case 0xe3: /* ex (sp), iz */
    z80->state.tmp = read_word_at(z80, z80->reg.sp);
    write_word_at(z80, *rr, z80->reg.sp);
    *rr = z80->state.tmp;
    return 23;

  default:
    return Z80E_INVALID_OPCODE;
  }
}

static void dec8(z80e* z80, zu8* reg) {
  set_hf(z80, u8_half_borrow(*reg, 1));
  set_cf(z80, borrow(8, *reg, 1, 0));
  set_pof(z80, cf(z80));
  *reg -= 1;
  set_sf(z80, u8_negative(*reg));
  set_zf(z80, *reg == 0);
  set_nf(z80, 1); /* Add = 0/Subtract = 1 */
  set_yf(z80, bit(*reg, 5));
  set_xf(z80, bit(*reg, 3));
}

static void inc8(z80e* z80, zu8* reg) {
  set_cf(z80, u8_overflow(*reg, 1));
  set_pof(z80, cf(z80));
  set_hf(z80, u8_half_carry(*reg, 1));
  *reg += 1;
  set_sf(z80, u8_negative(*reg));
  set_zf(z80, *reg == 0);
  set_yf(z80, bit(*reg, 5));
  set_xf(z80, bit(*reg, 3));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
}

static void add8(z80e* z80, zu8 v, zu8 c) {
  set_cf(z80, carry(8, reg(a), v, c));
  set_pof(z80, cf(z80));
  set_hf(z80, carry(4, reg(a), v, c));
  reg(a) = reg(a) + v + c;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
}

static void sub8(z80e* z80, zu8 v, zu8 c) {
  set_cf(z80, borrow(8, reg(a), v, c));
  set_pof(z80, cf(z80));
  set_hf(z80, borrow(4, reg(a), v, c));
  reg(a) = reg(a) - (v + c);
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 1); /* Add = 0/Subtract = 1 */
}

static void and8(z80e* z80, zu8 v) {
  reg(a) = reg(a) & v;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_hf(z80, 1);
  set_pof(z80, is_even_parity(reg(a)));
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 0);
  set_cf(z80, 0);
}

static void or8(z80e* z80, zu8 v) {
  reg(a) = reg(a) | v;
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_hf(z80, 0);
  set_xf(z80, bit(reg(a), 3));
  set_pof(z80, is_even_parity(reg(a)));
  set_nf(z80, 0);
  set_cf(z80, 0);
}

static void xor8(z80e* z80, zu8 v) {
  reg(a) = reg(a) ^ v;
  set_sf(z80, (reg(a) & 0x80) != 0); /* Is negative */
  set_zf(z80, reg(a) == 0);
  set_hf(z80, 0);
  set_pof(z80, is_even_parity(reg(a)));
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 0);
  set_cf(z80, 0);
}

static void cp8(z80e* z80, zu8 v) {
  z80->state.tmp = reg(a) - v;
  set_hf(z80, u8_half_borrow(reg(a), v));
  set_cf(z80, borrow(8, reg(a), v, 0));
  set_pof(z80, cf(z80));
  set_sf(z80, u8_negative(z80->state.tmp));
  set_zf(z80, reg(a) == v);
  set_yf(z80, bit(z80->state.tmp, 5));
  set_xf(z80, bit(z80->state.tmp, 3));
  set_nf(z80, 1);
}

static void inc_addr(z80e* z80, zu16 addr) {
  z80->state.tmp = read_byte_at(z80, addr);
  set_cf(z80, u8_overflow(z80->state.tmp, 1));
  set_pof(z80, cf(z80));
  set_hf(z80, u8_half_carry(z80->state.tmp, 1));
  z80->state.tmp += 1;
  set_sf(z80, u8_negative(z80->state.tmp));
  set_zf(z80, z80->state.tmp == 0);
  set_yf(z80, bit(z80->state.tmp, 5));
  set_xf(z80, bit(z80->state.tmp, 3));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
}

static void dec_addr(z80e* z80, zu16 addr) {
  z80->state.tmp = read_byte_at(z80, addr);
  set_hf(z80, u8_half_borrow(z80->state.tmp, 1));
  z80->state.tmp -= 1;
  set_pof(z80, z80->state.tmp == 0x7f); /* P/V is set if m was 80h before operation */
  set_sf(z80, u8_negative(z80->state.tmp));
  set_zf(z80, z80->state.tmp == 0);
  set_nf(z80, 1); /* Add = 0/Subtract = 1 */
  set_yf(z80, bit(z80->state.tmp, 5));
  set_xf(z80, bit(z80->state.tmp, 3));
}

static zu16 add16(z80e* z80, zu16 a, zu16 b) {
  zu16 res = a + b;
  set_yf(z80, bit(res, 13));
  set_hf(z80, u16_byte_carry(a, b));
  set_xf(z80, bit(res, 11));
  set_nf(z80, 0); /* Add = 0/Subtract = 1 */
  set_cf(z80, carry(15, a, b, 0));
  return res;
}

static zu16 inc16(z80e* z80, zu16 v) {
  v += 1;
  set_yf_u16(z80, v);
  set_xf_u16(z80, v);
  return v;
}

static zu16 dec16(z80e* z80, zu16 v) {
  v -= 1;
  set_yf_u16(z80, v);
  set_xf_u16(z80, v);
  return v;
}

static void jp(z80e* z80, zu8 cond) {
  if (cond) {
    z80->reg.pc = read_word(z80);
  } else {
    read_word(z80);
  }
}

static zu8 ldd(z80e* z80) {
  zu8 byte = read_byte_at(z80, hl(z80));
  write_byte_at(z80, byte, de(z80));
  set_de(z80, de(z80) - 1);
  set_hl(z80, hl(z80) - 1);
  set_bc(z80, bc(z80) - 1);
  set_yf(z80, bit(reg(a) + byte, 1));
  set_hf(z80, 0);
  set_xf(z80, bit(reg(a) + byte, 3));
  set_pof(z80, bc(z80) != 0);
  set_nf(z80, 0);
  return 16;
}

static void push(z80e* z80, zu16 rr) {
  write_byte_at(z80, msb(rr), --z80->reg.sp);
  write_byte_at(z80, lsb(rr), --z80->reg.sp);
}

static zu16 pop(z80e* z80) {
  z80->state.tmp = read_byte_at(z80, z80->reg.sp);
  z80->state.tmp |= (zu16)read_byte_at(z80, ++z80->reg.sp) << 8;
  z80->reg.sp += 1;
  return z80->state.tmp;
}

static zu8 ldi(z80e* z80) {
  zu8 byte = read_byte_at(z80, hl(z80));
  write_byte_at(z80, byte, de(z80));
  set_de(z80, de(z80) + 1);
  set_hl(z80, hl(z80) + 1);
  set_bc(z80, bc(z80) - 1);
  set_yf(z80, bit(reg(a) + byte, 1));
  set_hf(z80, 0);
  set_xf(z80, bit(reg(a) + byte, 3));
  set_pof(z80, bc(z80) != 0);
  set_nf(z80, 0);
  return 16;
}

static zu8 lddr(z80e* z80) {
  ldd(z80);
  if (bc(z80) != 0) {
    z80->reg.pc -= 2;
    return 21;
  }
  return 16;
}

static zu8 ldir(z80e* z80) {
  ldi(z80);
  if (bc(z80) != 0) {
    z80->reg.pc -= 2;
    return 21;
  }
  return 16;
}

static zu8 cpi(z80e* z80) {
  zu8 byte = read_byte_at(z80, hl(z80));
  set_hf(z80, borrow(4, reg(a), byte, 0));

  z80->state.tmp = byte = reg(a) - byte;
  set_sf(z80, u8_negative(byte));
  set_zf(z80, byte == 0);

  byte -= hf(z80);
  set_yf(z80, bit(byte, 1));
  set_xf(z80, bit(byte, 3));

  set_hl(z80, hl(z80) + 1);
  set_bc(z80, bc(z80) - 1);
  set_pof(z80, bc(z80) != 0);
  set_nf(z80, 1);
  return 16;
}

static zu8 cpir(z80e* z80) {
  cpi(z80);
  if (bc(z80) == 0 || z80->state.tmp == 0) {
    return 16;
  }
  z80->reg.pc -= 2;
  return 21;
}

static zu8 cpd(z80e* z80) {
  zu8 byte = read_byte_at(z80, hl(z80));
  set_hf(z80, borrow(4, reg(a), byte, 0));

  z80->state.tmp = byte = reg(a) - byte;
  set_sf(z80, u8_negative(byte));
  set_zf(z80, byte == 0);

  byte -= hf(z80);
  set_yf(z80, bit(byte, 1));
  set_xf(z80, bit(byte, 3));

  set_hl(z80, hl(z80) - 1);
  set_bc(z80, bc(z80) - 1);
  set_pof(z80, bc(z80) != 0);
  set_nf(z80, 1);
  return 16;
}

static zu8 cpdr(z80e* z80) {
  cpd(z80);
  if (bc(z80) == 0 || z80->state.tmp == 0) {
    return 16;
  }
  z80->reg.pc -= 2;
  return 21;
}

static zu8 is_even_parity(zu8 v) {
  zu8 n = 0;
  for (zu8 i = 0; i < 0x09; ++i) {
    n += ((v & (1 << i)) != 0);
  }
  return n % 2 == 0;
}

static zu8 jr(z80e* z80, zu8 cond) {
  if (cond) {
    z80->reg.pc += (zi8)read_byte(z80);
    return 12;
  }
  z80->reg.pc += 1;
  return 7;
}

static void daa(z80e* z80) {
  zu8 low = low_nibble(reg(a));
  zu8 corr = 0;

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

static void cpl(z80e* z80) {
  reg(a) = ~reg(a);
  set_yf(z80, bit(reg(a), 5));
  set_hf(z80, 1);
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 1);
}

static void neg(z80e* z80) {
  set_hf(z80, borrow(4, 0, reg(a), 0));
  set_pof(z80, reg(a) == 0x80);
  set_cf(z80, reg(a) != 0);
  reg(a) = -reg(a);
  set_sf(z80, u8_negative(reg(a)));
  set_zf(z80, reg(a) == 0);
  set_yf(z80, bit(reg(a), 5));
  set_xf(z80, bit(reg(a), 3));
  set_nf(z80, 1);
}

static void ccf(z80e* z80) {
  set_hf(z80, cf(z80));
  set_cf(z80, !cf(z80));
}

static void scf(z80e* z80) {
  set_hf(z80, 0);
  set_nf(z80, 0);
  set_cf(z80, 1);
}

#define rs_set_registers(n)                                                                                            \
  do {                                                                                                                 \
    set_sf(z80, bit(n, 7));                                                                                            \
    set_zf(z80, n == 0);                                                                                               \
    set_yf(z80, bit(n, 5));                                                                                            \
    set_hf(z80, 0);                                                                                                    \
    set_xf(z80, bit(n, 3));                                                                                            \
    set_pof(z80, is_even_parity(n));                                                                                   \
    set_nf(z80, 0);                                                                                                    \
  } while (0)

static void rlc(z80e* z80, zu8* r) {
  set_cf(z80, bit(*r, 7));
  *r = (*r << 1) | cf(z80);
  rs_set_registers(*r);
}

static void rl(z80e* z80, zu8* r) {
  z80->state.tmp = bit(*r, 7);
  *r = (*r << 1) | cf(z80);
  set_cf(z80, z80->state.tmp);
  rs_set_registers(*r);
}

static void rrc(z80e* z80, zu8* r) {
  zu8 tmp = bit(*r, 0);
  *r = (*r >> 1) | (tmp << 7);
  set_cf(z80, tmp);
  rs_set_registers(*r);
}

static void rr(z80e* z80, zu8* r) {
  zu8 tmp = bit(*r, 0);
  *r = (*r >> 1) | (cf(z80) << 7);
  set_cf(z80, tmp);
  rs_set_registers(*r);
}

static void sla(z80e* z80, zu8* r) {
  set_cf(z80, bit(*r, 7));
  *r = *r << 1;
  rs_set_registers(*r);
}

static void sra(z80e* z80, zu8* r) {
  zu8 tmp = *r & 0x80;
  set_cf(z80, bit(*r, 0));
  *r = (*r >> 1) | tmp;
  rs_set_registers(*r);
}

static void srl(z80e* z80, zu8* r) {
  set_cf(z80, bit(*r, 0));
  *r = *r >> 1;
  rs_set_registers(*r);
}

#undef rs_set_registers

zu8 call(z80e* z80, zu8 cc, zu16 nn) {
  if (cc) {
    push(z80, z80->reg.pc);
    z80->reg.pc = nn;
    return 17;
  }
  return 10;
}

zu8 ret(z80e* z80, zu8 cc) {
  if (cc) {
    z80->reg.pc = pop(z80);
    return 11;
  }
  return 10;
}

static void bit_op(z80e* z80, zu8 opcode, zu8 r) {
  zu8 bit_idx = (opcode >> 3) & 0x7;
  zu8 res = r & (1 << bit_idx);
  set_sf(z80, bit(res, 7));
  set_zf(z80, res == 0);
  set_yf(z80, bit(res, 5));
  set_hf(z80, 1);
  set_xf(z80, bit(res, 3));
  set_pof(z80, zf(z80));
  set_nf(z80, 0);
  /* cf is unchanged */
}

static void undoc_bit_hl(z80e* z80, zu8 opcode) {
  /* YF and XF are copied from some sort of internal register. This register
   * is related to 16-bit additions. - The Undocumented Z80 Documented v0.91 */
  zu8 tmp = read_byte_at(z80, hl(z80));
  bit_op(z80, opcode, tmp);
  set_yf(z80, bit(z80->reg.u, 5));
  set_xf(z80, bit(z80->reg.u, 3));
  /* XXX: implement UF */
}

static void set_op(z80e* z80, zu8 opcode, zu8* r) {
  zu8 bit_idx = (opcode >> 3) & 0x7;
  *r |= 1 << bit_idx;
}

static void res_op(z80e* z80, zu8 opcode, zu8* r) {
  zu8 bit_idx = (opcode >> 3) & 0x7;
  *r &= ~(1 << bit_idx);
}

static void set_bc(z80e* z80, zu16 val) {
  reg(b) = val >> 8;
  reg(c) = val;
}

static void set_hl(z80e* z80, zu16 val) {
  reg(h) = val >> 8;
  reg(l) = val;
}

static void set_de(z80e* z80, zu16 val) {
  reg(d) = val >> 8;
  reg(e) = val;
}

static void set_sp(z80e* z80, zu16 val) { z80->reg.sp = val; }

static void set_af(z80e* z80, zu16 val) {
  reg(a) = val >> 8;
  reg(f) = val;
}

static zu8 read_byte(z80e* z80) {
  zu8 b = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  return b;
}

static zu8 read_byte_at(z80e* z80, zu16 addr) { return z80->memread(addr, z80->ctx); }

static zu16 read_word(z80e* z80) {
  zu8 lsb = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  zu8 msb = z80->memread(z80->reg.pc, z80->ctx);
  z80->reg.pc += 1;
  return msb << 8 | lsb;
}

static zu16 read_word_at(z80e* z80, zu16 addr) { return read_byte_at(z80, addr) | (read_byte_at(z80, addr + 1) << 8); }

static void write_byte(z80e* z80, zu8 byte) {
  z80->memwrite(z80->reg.pc, byte, z80->ctx);
  z80->reg.pc += 1;
}

static void write_byte_at(z80e* z80, zu8 byte, zu16 addr) { z80->memwrite(addr, byte, z80->ctx); }

static void write_word(z80e* z80, zu16 word) {
  write_byte(z80, word);
  write_byte(z80, (word >> 8));
}

static void write_word_at(z80e* z80, zu16 word, zu16 addr) {
  write_byte_at(z80, word, addr);
  write_byte_at(z80, (word >> 8), addr + 1);
}
