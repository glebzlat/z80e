#include <z80wasm/emulator.h>

#define WASM_NAMESPACE "env"

#define IMPORT_MODULE(MODNAME) __attribute__((import_module(MODNAME)))

#define IX_HASH 0x7869
#define IY_HASH 0x7969
#define SP_HASH 0x7073
#define PC_HASH 0x6370

extern zu8 memread_fn(zu32 a, void *c) IMPORT_MODULE(WASM_NAMESPACE);
extern void memwrite_fn(zu32 a, zu8 b, void *c) IMPORT_MODULE(WASM_NAMESPACE);
extern zu8 ioread_fn(zu16 a, zu8 b, void* c) IMPORT_MODULE(WASM_NAMESPACE);
extern void iowrite_fn(zu16 a, zu8 b, void* c) IMPORT_MODULE(WASM_NAMESPACE);

static z80e_config _config;
static z80e _emu;

static zu32 hash(char const *s);

void init(void) {
  _config.memread = memread_fn;
  _config.memwrite = memwrite_fn;
  _config.ioread = ioread_fn;
  _config.iowrite = iowrite_fn;
  _config.ctx = (void *)0;

  z80e_init(&_emu, &_config);
}

void reset(void) { z80e_init(&_emu, &_config); }

zu8 get_register8(char r, int alt) {
  z80e_registers *regset = (alt ? &_emu.reg.alt : &_emu.reg.main);

  switch (r) {
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
    return 0;
  }
}

void set_register8(char r, zu8 v, int alt) {
  z80e_registers *regset = (alt ? &_emu.reg.alt : &_emu.reg.main);

  switch (r) {
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
  }
}

zu16 get_register16(char const *r) {
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
    return 0;
  }
}

void set_register16(char const *r, zu16 v) {
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
  }
}

static zu32 hash(char const *s) {
  /* Pretty simple function for strings with length <= 4 */
  zu32 v = 0;
  for (int i = 0; i < 4 && s[i] != '\0'; ++i) {
    v |= s[i] << (8 * i);
  }
  return v;
}
