#ifndef Z80E_H
#define Z80E_H

#include <stdint.h>

typedef uint8_t (*z80e_memread_fn_t)(uint32_t addr, void* ctx);
typedef void (*z80e_memwrite_fn_t)(uint32_t addr, uint8_t byte, void* ctx);
typedef uint8_t (*z80e_ioread_fn_t)(uint32_t addr, void* ctx);
typedef void (*z80e_iowrite_fn_t)(uint32_t addr, uint8_t byte, void* ctx);

typedef int8_t i8;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef struct {
  u8 a;
  u8 b;
  u8 d;
  u8 h;

  /* Flag register
   *
   * Flag bits: S Z Y H X P/V N C
   *
   * - S   - Sign flag
   * - Z   - Zero flag
   * - Y   - Copy of 5th bit of the result (undocumented)
   * - H   - Half-carry flag
   * - X   - Copy of 3rd bit of the result (undocumented)
   * - P/V - Parity/Overflow flag
   * - N   - Add/Subtract flag
   * - C   - Carry flag
   */
  u8 f;

  u8 c;
  u8 e;
  u8 l;
} z80e_registers;

typedef enum {
  Z80E_OK = 0,
  Z80E_DAA_INVALID_VALUE = -1,
  Z80E_INVALID_OPCODE = -2,
} z80_error_code;

struct z80e {
  struct reg {
    z80e_registers main;
    z80e_registers alt;
    z80e_registers* cur; /*< Points either to main or alt register set */

    u8 i;   /*< Interrupt Vector */
    u8 r;   /*< Memory Refresh */
    u16 ix; /*< Index Register X */
    u16 iy; /*< Index Register Y */
    u16 sp; /*< Stack Pointer */
    u16 pc; /*< Program Counter */
  } reg;

  u8 cur_reg_set; /*< 0 - main, 1 - alt */
  u8 halt;

  z80e_memread_fn_t memread;
  z80e_memwrite_fn_t memwrite;
  z80e_ioread_fn_t ioread;
  z80e_iowrite_fn_t iowrite;
  void* ctx;

  z80_error_code error;
};

struct z80e_config {
  z80e_memread_fn_t memread;
  z80e_memwrite_fn_t memwrite;
  z80e_ioread_fn_t ioread;
  z80e_iowrite_fn_t iowrite;
  void* ctx;
};

typedef struct z80e z80e;
typedef struct z80e_config z80e_config;

void z80e_init(z80e* z80, z80e_config* config);
int8_t z80e_instruction(z80e* z80);

void z80e_halt(z80e* z80);
int z80e_get_halt(z80e* z80);

#endif
