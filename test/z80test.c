#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <z80e.h>

typedef struct {
  FILE* memfile;
  FILE* iofile;
} s_context;

void memwrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t memread(uint32_t addr, void* ctx);
void iowrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t ioread(uint32_t addr, void* ctx);

char const* binfmt8(u8 v, char* buf);
void register_dump(z80e* z80);

int main(void) {
  FILE* memfile = fopen("./memfile", "rb+");
  if (memfile == NULL) {
    perror("fopen");
    return EXIT_FAILURE;
  }

  FILE* iofile = fopen("./iofile", "rb");
  if (iofile == NULL) {
    perror("fopen");
    return EXIT_FAILURE;
  }

  s_context ctx = {
      .memfile = memfile,
      .iofile = iofile,
  };

  z80e_config cfg = {
      .memwrite = memwrite,
      .memread = memread,
      .iowrite = iowrite,
      .ioread = ioread,
      .ctx = &ctx,
  };

  z80e z80;
  z80e_init(&z80, &cfg);

  while (!z80e_get_halt(&z80)) {
    z80e_instruction(&z80);
  }

  register_dump(&z80);

  fclose(memfile);
  fclose(iofile);
  return 0;
}

void memwrite(uint32_t addr, uint8_t byte, void* ctx) {
  s_context* c = ctx;
  uint8_t buf[1];
  buf[0] = byte;
  fseek(c->memfile, addr, SEEK_SET);
  fwrite(buf, 1, 1, c->memfile);
}

uint8_t memread(uint32_t addr, void* ctx) {
  s_context* c = ctx;
  uint8_t buf[1];
  fseek(c->memfile, addr, SEEK_SET);
  fread(buf, 1, 1, c->memfile);
  return buf[0];
}

void iowrite(uint32_t addr, uint8_t byte, void* ctx) {
  s_context* c = ctx;
  uint8_t buf[1];
  buf[0] = byte;
  fseek(c->iofile, addr, SEEK_SET);
  fwrite(buf, 1, 1, c->iofile);
}

uint8_t ioread(uint32_t addr, void* ctx) {
  s_context* c = ctx;
  uint8_t buf[1];
  fseek(c->iofile, addr, SEEK_SET);
  fread(buf, 1, 1, c->iofile);
  return buf[0];
}

char const* binfmt8(u8 v, char* buf) {
  buf[0] = '0';
  buf[1] = 'b';
  for (int i = 0; i < 8; ++i) {
    buf[i + 2] = ((v & (1 << (7 - i))) == 0) ? '0' : '1';
  }
  buf[10] = '\0';
  return buf;
}

void register_dump(z80e* z80) {
  char buf[11];
#define PRINTREG(NAME)                                                                                                 \
  do {                                                                                                                 \
    printf(#NAME "\t%s\t", binfmt8(z80->reg.main.NAME, buf));                                                          \
    printf(#NAME "'\t%s\n", binfmt8(z80->reg.alt.NAME, buf));                                                          \
  } while (0)

  PRINTREG(a);
  PRINTREG(b);
  PRINTREG(c);
  PRINTREG(d);
  PRINTREG(e);
  PRINTREG(f);
  PRINTREG(h);
  PRINTREG(l);

#undef PRINTREG
#define PRINTREG(REG1, REG2)                                                                                           \
  printf(#REG1 "\t%s\t" #REG2 "\t%s\n", binfmt8(z80->reg.REG1, buf), binfmt8(z80->reg.REG2, buf))

  PRINTREG(i, r);
  PRINTREG(ix, iy);
  PRINTREG(sp, pc);

#undef PRINTREG
}
