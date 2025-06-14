#include "z80e.h"

#define MAXU8 ((u8) - 1)
#define MAXI8 ((i8) - 129)
#define MINI8 ((i8)128)

#define reg(name) (z80->reg.cur->name)

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
  case 0x00: /* nop */
    return 4;
  case 0x01: /* ld bc, nn */
    set_bc(z80, read_word(z80));
    return 10;
  case 0x02: /* ld bc, a */
    set_bc(z80, reg(a));
    return 7;
  case 0x03: /* inc bc */
    set_bc(z80, bc(z80) + 1);
    return 6;
  case 0x04: /* inc b */
    return inc8(z80, &reg(b));
  case 0x05: /* dec b */
    return dec8(z80, &reg(b));
  case 0x06: /* ld b, n */
    reg(b) = read_byte(z80);
    return 7;
  case 0x07: /* rlca */
    set_cf(z80, (reg(a) & 0x80) != 0);
    reg(a) = (reg(a) << 1) | cf(z80);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, (reg(a) & (1 << 5)) != 0);
    set_xf(z80, (reg(a) & (1 << 3)) != 0);
    return 4;
  case 0x08: /* ex af, af' */
    tmp8 = z80->reg.main.a;
    z80->reg.alt.a = z80->reg.main.a;
    z80->reg.main.a = tmp8;
    tmp8 = z80->reg.main.f;
    z80->reg.alt.f = z80->reg.main.f;
    z80->reg.main.f = tmp8;
    return 4;
  case 0x09: /* add hl, bc */
    set_hl(z80, add16(z80, hl(z80), bc(z80)));
    return 11;
  case 0x0a: /* ld a, (bc) */
    reg(a) = read_byte_at(z80, bc(z80));
    return 7;
  case 0x0b: /* dec bc */
    set_bc(z80, bc(z80) - 1);
    return 6;
  case 0x0c: /* inc c */
    return inc8(z80, &reg(c));
  case 0x0d: /* dec c */
    return dec8(z80, &reg(c));
  case 0x0e: /* ld c, n */
    reg(c) = read_byte(z80);
    return 7;
  case 0x0f: /* rrca */
    set_cf(z80, (reg(a) & 0x01) != 0);
    set_nf(z80, 0);
    set_hf(z80, 0);
    reg(a) = (reg(a) >> 1) | (cf(z80) << 7);
    return 4;
  case 0x10: /* djnz d */
    reg(b) = reg(b) - 1;
    return jr(z80, reg(b) != 0) + 1;
  case 0x11: /* ld de, nn */
    set_de(z80, read_word(z80));
    return 10;
  case 0x12: /* ld (de), a */
    write_byte_at(z80, reg(a), de(z80));
    return 7;
  case 0x13: /* inc de */
    set_de(z80, de(z80) + 1);
    return 6;
  case 0x14: /* inc d */
    return inc8(z80, &reg(d));
  case 0x15: /* dec d */
    return dec8(z80, &reg(d));
  case 0x16: /* ld d, n */
    reg(d) = read_byte(z80);
    return 7;
  case 0x17: /* rla */
    tmp8 = (reg(a) & 0x80) != 0;
    reg(a) = (reg(a) << 1) | (cf(z80) & 0x01);
    set_cf(z80, tmp8);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, (reg(a) & (1 << 5)) != 0);
    set_xf(z80, (reg(a) & (1 << 3)) != 0);
    return 4;
  case 0x18: /* jr d */
    return jr(z80, 1);
  case 0x19: /* add hl, de */
    set_hl(z80, add16(z80, hl(z80), de(z80)));
    return 11;
  case 0x1a: /* ld a, (de) */
    reg(a) = read_byte_at(z80, de(z80));
    return 7;
  case 0x1b: /* dec de */
    set_de(z80, de(z80) - 1);
    return 6;
  case 0x1c: /* inc e */
    return inc8(z80, &reg(e));
  case 0x1d: /* dec e */
    return dec8(z80, &reg(e));
  case 0x1e: /* ld e, n */
    reg(e) = read_byte(z80);
    return 7;
  case 0x1f: /* rra */
    tmp8 = (reg(a) & 0x01) != 0;
    reg(a) = (cf(z80) & 0x80) | (reg(a) >> 1);
    set_cf(z80, tmp8);
    set_nf(z80, 0);
    set_hf(z80, 0);
    set_yf(z80, (reg(a) & (1 << 5)) != 0);
    set_xf(z80, (reg(a) & (1 << 3)) != 0);
    return 4;
  case 0x20: /* jr nz, d */
    return jr(z80, !zf(z80));
  case 0x21: /* ld hl, nn */
    set_hl(z80, read_word(z80));
    return 10;
  case 0x22: /* ld (nn), hl */
    write_word(z80, hl(z80));
    return 16;
  case 0x23: /* inc hl */
    set_hl(z80, hl(z80) + 1);
    return 6;
  case 0x24: /* inc h */
    return inc8(z80, &reg(h));
  case 0x25: /* dec h */
    return dec8(z80, &reg(h));
  case 0x26: /* ld h, n */
    reg(h) = read_byte(z80);
    return 7;
  case 0x27: /* daa */
    daa(z80);
    return 4;
  case 0x28: /* jr z, d */
    return jr(z80, zf(z80));
  case 0x29: /* add hl, hl */
    set_hl(z80, add16(z80, hl(z80), hl(z80)));
    return 11;
  case 0x2a: /* ld hl, (nn) */
    set_hl(z80, read_word_at(z80, read_word(z80)));
    return 16;
  case 0x2b: /* dec hl */
    set_hl(z80, hl(z80) - 1);
    return 6;
  case 0x2c: /* inc l */
    return inc8(z80, &reg(l));
  case 0x2d: /* dec l */
    return dec8(z80, &reg(l));
  case 0x2e: /* ld l, n */
    reg(l) = read_byte(z80);
    return 7;
  case 0x2f: /* cpl */
    reg(a) = ~reg(a) + 1;
    return 4;
  case 0x30: /* jr nc, d */
    return jr(z80, !cf(z80));
  case 0x31: /* ld sp, nn */
    z80->reg.sp = read_word(z80);
    return 10;
  case 0x32: /* ld (nn), a */
    write_byte_at(z80, reg(a), read_word(z80));
    return 13;
  case 0x33: /* inc sp */
    z80->reg.sp += 1;
    return 6;
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
  case 0x36: /* ld (hl), n */
    write_byte_at(z80, read_byte(z80), hl(z80));
    return 10;
  case 0x37: /* scf */
    set_cf(z80, 1);
    return 4;
  case 0x38: /* jr c, d */
    return jr(z80, cf(z80));
  case 0x39: /* add hl, sp */
    set_hl(z80, add16(z80, hl(z80), z80->reg.sp));
    return 11;
  case 0x3a: /* ld a, (nn) */
    reg(a) = read_byte_at(z80, read_word(z80));
    return 13;
  case 0x3b: /* dec sp */
    z80->reg.sp -= 1;
    return 6;
  case 0x3c: /* inc a */
    return inc8(z80, &reg(a));
  case 0x3d: /* dec a */
    return dec8(z80, &reg(a));
  case 0x3e: /* ld a, n */
    reg(a) = read_byte(z80);
    return 7;
  case 0x3f: /* ccf */
    set_cf(z80, !cf(z80));
    return 4;
  case 0x40: /* ld b, b */
    return 4;
  case 0x41: /* ld b, c */
    reg(b) = reg(c);
    return 4;
  case 0x42: /* ld b, d */
    reg(b) = reg(d);
    return 4;
  case 0x43: /* ld b, e */
    reg(b) = reg(e);
    return 4;
  case 0x44: /* ld b, h */
    reg(b) = reg(h);
    return 4;
  case 0x45: /* ld b, l */
    reg(b) = reg(l);
    return 4;
  case 0x46: /* ld b, (hl) */
    reg(b) = read_byte_at(z80, hl(z80));
    return 7;
  case 0x47: /* ld b, a */
    reg(b) = reg(a);
    return 4;
  case 0x48: /* ld c, b */
    reg(c) = reg(b);
    return 4;
  case 0x49: /* ld c, c */
    return 4;
  case 0x4a: /* ld c, d */
    reg(c) = reg(d);
    return 4;
  case 0x4b: /* ld c, e */
    reg(c) = reg(e);
    return 4;
  case 0x4c: /* ld c, h */
    reg(c) = reg(h);
    return 4;
  case 0x4d: /* ld c, l */
    reg(c) = reg(l);
    return 4;
  case 0x4e: /* ld c, (hl) */
    reg(c) = read_byte_at(z80, hl(z80));
    return 7;
  case 0x4f: /* ld c, a */
    reg(c) = reg(a);
    return 4;
  case 0x50: /* ld d, b */
    reg(d) = reg(b);
    return 4;
  case 0x51: /* ld d, c */
    reg(d) = reg(c);
    return 4;
  case 0x52: /* ld d, d */
    return 4;
  case 0x53: /* ld d, e */
    reg(d) = reg(e);
    return 4;
  case 0x54: /* ld d, h */
    reg(d) = reg(h);
    return 4;
  case 0x55: /* ld d, l */
    reg(d) = reg(l);
    return 4;
  case 0x56: /* ld d, (hl) */
    reg(d) = read_byte_at(z80, hl(z80));
    return 7;
  case 0x57: /* ld d, a */
    reg(d) = reg(a);
    return 4;
  case 0x58: /* ld e, b */
    reg(e) = reg(b);
    return 4;
  case 0x59: /* ld e, c */
    return 4;
  case 0x5a: /* ld e, d */
    reg(e) = reg(d);
    return 4;
  case 0x5b: /* ld e, e */
    return 4;
  case 0x5c: /* ld e, h */
    reg(e) = reg(h);
    return 4;
  case 0x5d: /* ld e, l */
    reg(e) = reg(l);
    return 4;
  case 0x5e: /* ld e, (hl) */
    reg(e) = read_byte_at(z80, hl(z80));
    return 6;
  case 0x5f: /* ld e, a */
    reg(e) = reg(a);
    return 4;
  case 0x60: /* ld h, b */
    reg(h) = reg(b);
    return 4;
  case 0x61: /* ld h, c */
    reg(h) = reg(c);
    return 4;
  case 0x62: /* ld h, d */
    reg(h) = reg(d);
    return 4;
  case 0x63: /* ld h, e */
    reg(h) = reg(e);
    return 4;
  case 0x64: /* ld h, h */
    return 4;
  case 0x65: /* ld h, l */
    reg(h) = reg(l);
    return 4;
  case 0x66: /* ld h, (hl) */
    reg(h) = read_byte_at(z80, hl(z80));
    return 7;
  case 0x67: /* ld h, a */
    reg(h) = reg(a);
    return 4;
  case 0x68: /* ld l, b */
    reg(l) = reg(b);
    return 4;
  case 0x69: /* ld l, c */
    return 4;
  case 0x6a: /* ld l, d */
    reg(l) = reg(d);
    return 4;
  case 0x6b: /* ld l, e */
    reg(l) = reg(e);
    return 4;
  case 0x6c: /* ld l, h */
    reg(l) = reg(h);
    return 4;
  case 0x6d: /* ld l, l */
    return 4;
  case 0x6e: /* ld l, (hl) */
    reg(l) = read_byte_at(z80, hl(z80));
    return 6;
  case 0x6f: /* ld l, a */
    reg(l) = reg(a);
    return 4;
  case 0x70: /* ld (hl), b */
    write_byte_at(z80, reg(b), hl(z80));
    return 7;
  case 0x71: /* ld (hl), c */
    write_byte_at(z80, reg(c), hl(z80));
    return 7;
  case 0x72: /* ld (hl), d */
    write_byte_at(z80, reg(d), hl(z80));
    return 7;
  case 0x73: /* ld (hl), e */
    write_byte_at(z80, reg(e), hl(z80));
    return 7;
  case 0x74: /* ld (hl), h */
    write_byte_at(z80, reg(h), hl(z80));
    return 7;
  case 0x75: /* ld (hl), l */
    write_byte_at(z80, reg(l), hl(z80));
    return 7;
  case 0x76: /* halt */
    z80->halt = 1;
    return 4;
  case 0x77: /* ld (hl), a */
    write_byte_at(z80, reg(a), hl(z80));
    return 7;
  case 0x78: /* ld a, b */
    reg(a) = reg(b);
    return 4;
  case 0x79: /* ld a, c */
    reg(a) = reg(c);
    return 4;
  case 0x7a: /* ld a, d */
    reg(a) = reg(d);
    return 4;
  case 0x7b: /* ld a, e */
    reg(a) = reg(e);
    return 4;
  case 0x7c: /* ld a, h */
    reg(a) = reg(h);
    return 4;
  case 0x7d: /* ld a, l */
    reg(a) = reg(l);
    return 4;
  case 0x7e: /* ld a, (hl) */
    reg(a) = read_byte_at(z80, hl(z80));
    return 7;
  case 0x7f: /* ld a, a */
    return 4;
  case 0x80: /* add a, b */
    return add8(z80, reg(b));
  case 0x81: /* add a, c */
    return add8(z80, reg(c));
  case 0x82: /* add a, d */
    return add8(z80, reg(d));
  case 0x83: /* add a, e */
    return add8(z80, reg(e));
  case 0x84: /* add a, h */
    return add8(z80, reg(h));
  case 0x85: /* add a, l */
    return add8(z80, reg(l));
  case 0x86: /* add a, (hl) */
    return add8(z80, read_byte_at(z80, hl(z80))) + 3;
  case 0x87: /* add a, a */
    return add8(z80, reg(a));
  case 0x88: /* adc a, b */
    return add8(z80, reg(b) + cf(z80));
  case 0x89: /* adc a, c */
    return add8(z80, reg(c) + cf(z80));
  case 0x8a: /* adc a, d */
    return add8(z80, reg(d) + cf(z80));
  case 0x8b: /* adc a, e */
    return add8(z80, reg(e) + cf(z80));
  case 0x8c: /* adc a, h */
    return add8(z80, reg(h) + cf(z80));
  case 0x8d: /* adc a, l */
    return add8(z80, reg(l) + cf(z80));
  case 0x8e: /* adc a, (hl) */
    return add8(z80, read_byte_at(z80, hl(z80)) + cf(z80));
  case 0x8f: /* adc a, a */
    return add8(z80, reg(a) + cf(z80));
  case 0x90: /* sub b */
    return sub8(z80, reg(b));
  case 0x91: /* sub c */
    return sub8(z80, reg(c));
  case 0x92: /* sub d */
    return sub8(z80, reg(d));
  case 0x93: /* sub e */
    return sub8(z80, reg(e));
  case 0x94: /* sub h */
    return sub8(z80, reg(h));
  case 0x95: /* sub l */
    return sub8(z80, reg(l));
  case 0x96: /* sub (hl) */
    return sub8(z80, read_byte_at(z80, hl(z80))) + 3;
  case 0x97: /* sub a */
    return sub8(z80, reg(a));
  case 0x98: /* sbc a, b */
    return sub8(z80, reg(b) + cf(z80));
  case 0x99: /* sbc a, c */
    return sub8(z80, reg(c) + cf(z80));
  case 0x9a: /* sbc a, d */
    return sub8(z80, reg(d) + cf(z80));
  case 0x9b: /* sbc a, e */
    return sub8(z80, reg(e) + cf(z80));
  case 0x9c: /* sbc a, h */
    return sub8(z80, reg(h) + cf(z80));
  case 0x9d: /* sbc a, l */
    return sub8(z80, reg(l) + cf(z80));
  case 0x9e: /* sbc a, (hl) */
    return sub8(z80, read_byte_at(z80, hl(z80)) + cf(z80)) + 3;
  case 0x9f: /* sbc a, a */
    return sub8(z80, reg(a) + cf(z80));
  case 0xa0: /* and b */
    return and8(z80, reg(b));
  case 0xa1: /* and c */
    return and8(z80, reg(c));
  case 0xa2: /* and d */
    return and8(z80, reg(d));
  case 0xa3: /* and e */
    return and8(z80, reg(e));
  case 0xa4: /* and h */
    return and8(z80, reg(h));
  case 0xa5: /* and l */
    return and8(z80, reg(l));
  case 0xa6: /* and (hl) */
    return and8(z80, read_byte_at(z80, hl(z80))) + 3;
  case 0xa7: /* and a */
    return and8(z80, reg(a));
  case 0xa8: /* xor b */
    return xor8(z80, reg(b));
  case 0xa9: /* xor c */
    return xor8(z80, reg(c));
  case 0xaa: /* xor d */
    return xor8(z80, reg(d));
  case 0xab: /* xor e */
    return xor8(z80, reg(e));
  case 0xac: /* xor h */
    return xor8(z80, reg(h));
  case 0xad: /* xor l */
    return xor8(z80, reg(l));
  case 0xae: /* xor (hl) */
    return xor8(z80, read_byte_at(z80, hl(z80))) + 3;
  case 0xaf: /* xor a */
    return xor8(z80, reg(a));
  case 0xd9: /* exx */
    z80->reg.cur = z80->cur_reg_set == 0 ? &z80->reg.main : &z80->reg.alt;
    z80->cur_reg_set = !z80->cur_reg_set;
    return 4;
  default:
    return Z80E_INVALID_OPCODE;
  };
}

static u8 dec8(z80e* z80, u8* reg) {
  set_hf(z80, (*reg & (1 << 3)) == 0); /* Will borrow from 3-rd bit */
  set_pof(z80, *reg == 0x80);          /* Will overflow */
  *reg -= 1;
  set_sf(z80, (*reg & 0x80) != 0);     /* Is negative */
  set_zf(z80, *reg == 0);              /* Is zero */
  set_nf(z80, 1);                      /* Add = 0/Subtract = 1 */
  set_cf(z80, 0);                      /* Carry unaffected */
  set_yf(z80, (*reg & (1 << 5)) != 0); /* Copy of the 5-th bit */
  set_xf(z80, (*reg & (1 << 3)) != 0); /* Copy of the 3-rd bit */
  return 4;
}

static u8 inc8(z80e* z80, u8* reg) {
  set_hf(z80, (*reg & 0x0F) == 0x0F); /* Will carry from the 3-rd bit */
  set_pof(z80, *reg == 0xff);         /* Will overflow */
  *reg += 1;
  set_sf(z80, (*reg & 0x80) != 0);     /* Is negative */
  set_zf(z80, *reg == 0);              /* Is zero */
  set_nf(z80, 0);                      /* Add = 0/Subtract = 1 */
  set_yf(z80, (*reg & (1 << 5)) != 0); /* Copy of the 5-th bit */
  set_xf(z80, (*reg & (1 << 3)) != 0); /* Copy of the 3-rd bit */
  // TODO: carry flag!
  return 4;
}

static u8 add8(z80e* z80, u8 v) {
  u8 tmp = reg(a);
  reg(a) += v;
  set_sf(z80, (reg(a) & 0x80) != 0);              /* Is negative */
  set_zf(z80, reg(a) == 0);                       /* Is zero */
  set_yf(z80, (reg(a) & (1 << 5)) != 0);          /* Copy of the 5-th bit */
  set_hf(z80, (tmp & 0x0f) + (v & 0x0f) == 0x10); /* Carry from the 3-rd bit */
  set_xf(z80, (reg(a) & (1 << 3)) != 0);          /* Copy of the 3-rd bit */
  set_nf(z80, 0);                                 /* Add = 1/Subtract = 0 */
  set_cf(z80, reg(a) < tmp || reg(a) < v);
  return 4;
}

static u8 sub8(z80e* z80, u8 v) {
  set_hf(z80, (reg(a) & (1 << 3)) == 0); /* Will borrow from 3-rd bit */
  set_pof(z80, reg(a) < v);              /* Will overflow */
  set_cf(z80, reg(a) < v);               /* Will borrow */
  reg(a) -= v;
  set_sf(z80, (reg(a) & 0x80) != 0);     /* Is negative */
  set_zf(z80, reg(a) == 0);              /* Is zero */
  set_yf(z80, (reg(a) & (1 << 5)) != 0); /* Copy of the 5-th bit */
  set_xf(z80, (reg(a) & (1 << 3)) != 0); /* Copy of the 3-rd bit */
  set_nf(z80, 1);                        /* Add = 0/Subtract = 1 */
  return 4;
}

static u8 and8(z80e* z80, u8 v) {
  u8 tmp = reg(a);
  reg(a) = reg(a) & v;
  set_sf(z80, (reg(a) & 0x80) != 0); /* Is negative */
  set_zf(z80, reg(a) == 0);          /* Is zero */
  set_hf(z80, 1);
  set_pof(z80, reg(a) < tmp || reg(a) < v); /* Is overflowed */
  set_yf(z80, (reg(a) & (1 << 5)) != 0);    /* Copy of the 5-th bit */
  set_xf(z80, (reg(a) & (1 << 3)) != 0);    /* Copy of the 3-rd bit */
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
  set_yf(z80, (reg(a) & (1 << 5)) != 0);
  set_xf(z80, (reg(a) & (1 << 3)) != 0);
  set_nf(z80, 0);
  set_cf(z80, 0);
  return 4;
}

static u16 add16(z80e* z80, u16 a, u16 b) {
  u16 res = a + b;
  /* Flags are affected by the high-byte addition */
  u8 msba = (a >> 8), msbb = (b >> 8), msbr = (res >> 8);
  set_yf(z80, (msbr & (1 << 5)) != 0);                /* Copy of the 5-th bit */
  set_hf(z80, (msba & 0x0F) + (msbb & 0x0F) == 0x10); /* Carry from the 3-rd bit */
  set_xf(z80, (msbr & (1 << 3)) != 0);                /* Copy of the 3-rd bit */
  set_nf(z80, 0);                                     /* Add = 1/Subtract = 0 */
  set_cf(z80, msbr < msba || msbr < msbb);            /* Is overflowed */
  return res;
}

static u8 is_even_parity(u8 v) {
  u8 n = 0;
  for (u8 i = 0; i < 0x09; ++i) {
    n += ((v & (1 << i)) == 1);
  }
  return n % 2 == 0;
}

static u8 jr(z80e* z80, u8 cond) {
  if (cond) {
    z80->reg.pc += read_byte(z80);
    z80->reg.pc -= 1;
    return 12;
  }
  z80->reg.pc += 1;
  return 7;
}

static void daa(z80e* z80) {
  u8 a = reg(a), c = cf(z80), h = hf(z80);

#define a_in(n, m) (a >= n && a <= m)
  if (a_in(0x00, 0x99) && !c && !h) {
    a += 0x00;
  } else if (a_in(0x0a, 0x8f) && !c && !h) {
    a += 0x06;
  } else if (a_in(0x00, 0x93) && !c && h) {
    a += 0x06;
  } else if (a_in(0xa0, 0xf9) && !c && !h) {
    a += 0x60;
    c = 1;
  } else if (a_in(0x9a, 0xff) && !c && !h) {
    a += 0x66;
    c = 1;
  } else if (a_in(0xa0, 0xf3) && !c && h) {
    a += 0x66;
    c = 1;
  } else if (a_in(0x00, 0x29) && c && !h) {
    a += 0x60;
  } else if (a_in(0x0a, 0x2f) && c && !h) {
    a += 0x66;
  } else if (a_in(0x00, 0x33) && c && h) {
    a += 0x66;
  } else if (a_in(0x00, 0x99) && !c && !h) {
    a += 0x00;
  } else if (a_in(0x06, 0x8f) && !c && h) {
    a += 0xfa;
  } else if (a_in(0x70, 0xf9) && c && !h) {
    a += 0xa0;
  } else if (a_in(0x66, 0x7f) && c && h) {
    a += 0x9a;
  } else {
    z80->error = Z80E_DAA_INVALID_VALUE;
    return;
  }
  set_cf(z80, c);
#undef a_in
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
