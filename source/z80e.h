#ifndef Z80E_H
#define Z80E_H

typedef char zi8;
typedef unsigned char zu8;
typedef short zi16;
typedef unsigned short zu16;
typedef int zi32;
typedef unsigned int zu32;

typedef zu8 (*z80e_memread_fn_t)(zu32 addr, void* ctx);
typedef void (*z80e_memwrite_fn_t)(zu32 addr, zu8 byte, void* ctx);
typedef zu8 (*z80e_ioread_fn_t)(zu32 addr, void* ctx);
typedef void (*z80e_iowrite_fn_t)(zu32 addr, zu8 byte, void* ctx);

typedef struct {
  zu8 a;
  zu8 b;
  zu8 d;
  zu8 h;

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
  zu8 f;

  zu8 c;
  zu8 e;
  zu8 l;
} z80e_registers;

typedef struct {
  zu16 tmp;
} z80e_state;

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

    zu8 i;   /*< Interrupt Vector */
    zu8 r;   /*< Memory Refresh */
    zu16 ix; /*< Index Register X */
    zu16 iy; /*< Index Register Y */
    zu16 sp; /*< Stack Pointer */
    zu16 pc; /*< Program Counter */
  } reg;

  zu8 cur_reg_set; /*< 0 - main, 1 - alt */
  zu8 halt;
  zu8 iff;
  zu8 int_mode;
  z80e_state state;

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
zi8 z80e_instruction(z80e* z80);

void z80e_halt(z80e* z80);
int z80e_get_halt(z80e* z80);

#endif
