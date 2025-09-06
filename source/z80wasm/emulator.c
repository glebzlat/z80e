#include <z80wasm/emulator.h>

#define WASM_NAMESPACE "env"

#define WASM_IMPORT(NAME) __attribute__((import_module(WASM_NAMESPACE), import_name(#NAME))) NAME

#define IX_HASH 0x7869
#define IY_HASH 0x7969
#define SP_HASH 0x7073
#define PC_HASH 0x6370

zu8 WASM_IMPORT(memread_fn)(zu32 a, void* c);
void WASM_IMPORT(memwrite_fn)(zu32 a, zu8 b, void* c);
zu8 WASM_IMPORT(ioread_fn)(zu16 a, zu8 b, void* c);
void WASM_IMPORT(iowrite_fn)(zu16 a, zu8 b, void* c);

extern unsigned char __heap_base;

static z80e_config _config;
static z80e _emu;
static status_type _status;
static unsigned int _bump_ptr = (unsigned int)&__heap_base;

static zu32 hash(char const* s);

void init(void) {
  _config.memread = memread_fn;
  _config.memwrite = memwrite_fn;
  _config.ioread = ioread_fn;
  _config.iowrite = iowrite_fn;
  _config.ctx = (void*)0;

  z80e_init(&_emu, &_config);
}

void reset(void) { z80e_init(&_emu, &_config); }

zi8 execute_instruction(void) {
  zi8 ret = z80e_instruction(&_emu);
  switch (ret) {
  case Z80E_DAA_INVALID_VALUE:
    _status = STATUS_ERROR_DAA_INVALID_VALUE;
    break;
  case Z80E_INVALID_OPCODE:
    _status = STATUS_ERROR_INVALID_OPCODE;
    break;
  }
  return ret;
}

void* allocate(int n) {
  unsigned int ptr = _bump_ptr;
  _bump_ptr += n;
  return (void*)ptr;
}

status_type get_status(void) {
  status_type tmp = _status;
  _status = STATUS_OK;
  return tmp;
}

zu8 get_register8(char const* r, int alt) {
  z80e_registers* regset = (alt ? &_emu.reg.alt : &_emu.reg.main);

  switch (r[0]) {
  case 'a':
    return regset->a;
  case 'b':
    return regset->b;
  case 'c':
    return regset->c;
  case 'd':
    return regset->d;
  case 'e':
    return regset->e;
  case 'h':
    return regset->h;
  case 'l':
    return regset->l;
  case 'f':
    return regset->f;
  case 'i':
    return _emu.reg.i;
  case 'r':
    return _emu.reg.r;
  case 'u':
    return _emu.reg.u;
  default:
    _status = STATUS_ERROR_NO_REGISTER;
    return 0;
  }
}

void set_register8(char const* r, zu8 v, int alt) {
  z80e_registers* regset = (alt ? &_emu.reg.alt : &_emu.reg.main);

  switch (r[0]) {
  case 'a':
    regset->a = v;
    break;
  case 'b':
    regset->b = v;
    break;
  case 'c':
    regset->c = v;
    break;
  case 'd':
    regset->d = v;
    break;
  case 'e':
    regset->e = v;
    break;
  case 'h':
    regset->h = v;
    break;
  case 'l':
    regset->l = v;
    break;
  case 'f':
    regset->f = v;
    break;
  case 'i':
    _emu.reg.i = v;
    break;
  case 'r':
    _emu.reg.r = v;
    break;
  case 'u':
    _emu.reg.u = v;
    break;
  default:
    _status = STATUS_ERROR_NO_REGISTER;
    break;
  }
}

zu16 get_register16(char const* r) {
  switch (hash(r)) {
  case IX_HASH:
    return _emu.reg.ix;
  case IY_HASH:
    return _emu.reg.iy;
  case SP_HASH:
    return _emu.reg.sp;
  case PC_HASH:
    return _emu.reg.pc;
  default:
    _status = STATUS_ERROR_NO_REGISTER;
    return 0;
  }
}

void set_register16(char const* r, zu16 v) {
  switch (hash(r)) {
  case IX_HASH:
    _emu.reg.ix = v;
    break;
  case IY_HASH:
    _emu.reg.iy = v;
    break;
  case SP_HASH:
    _emu.reg.sp = v;
    break;
  case PC_HASH:
    _emu.reg.pc = v;
    break;
  default:
    _status = STATUS_ERROR_NO_REGISTER;
    break;
  }
}

int is_halted(void) {
  return _emu.halt;
}

static zu32 hash(char const* s) {
  /* Pretty simple function for strings with length <= 4 */
  zu32 v = 0;
  for (int i = 0; i < 4 && s[i] != '\0'; ++i) {
    v |= s[i] << (8 * i);
  }
  return v;
}
