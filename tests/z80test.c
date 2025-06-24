#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <z80e.h>

typedef struct {
  FILE* memfile;
  FILE* iofile;
} s_context;

void memwrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t memread(uint32_t addr, void* ctx);
void iowrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t ioread(uint32_t addr, void* ctx);

char* alloc_binfmt_buffer(int max_bits);
char const* binfmt(uint32_t v, int width, char* buf);
void register_dump(z80e* z80);

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: z80test memfile iofile\n");
    return EXIT_FAILURE;
  }

  char const *mem_filename = argv[1], *io_filename = argv[2];

  FILE* memfile = fopen(mem_filename, "rb+");
  if (memfile == NULL) {
    fprintf(stderr, "cannot open file %s: %s\n", mem_filename, strerror(errno));
    return EXIT_FAILURE;
  }

  FILE* iofile = fopen(io_filename, "rb");
  if (iofile == NULL) {
    fprintf(stderr, "cannot open file %s: %s\n", mem_filename, strerror(errno));
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
    if (z80e_instruction(&z80) == Z80E_INVALID_OPCODE) {
      fprintf(stderr, "at 0x%04x: invalid instruction opcode\n", z80.reg.pc);
      fclose(memfile);
      fclose(iofile);
      return EXIT_FAILURE;
    }
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

char* alloc_binfmt_buffer(int width) {
  char* buf = malloc(sizeof(*buf) * (width + 3));
  assert(buf != NULL);
  return buf;
}

char const* binfmt(uint32_t v, int width, char* buf) {
  buf[0] = '0';
  buf[1] = 'b';
  int i;
  for (i = 0; i < width; ++i) {
    char bit;
    if (v) {
      bit = (v & 1) == 0 ? '0' : '1';
      v = v >> 1;
    } else {
      bit = '0';
    }
    buf[width - i + 1] = bit;
  }
  buf[i + 2] = '\0';
  return buf;
}

void register_dump(z80e* z80) {
  char* buf = alloc_binfmt_buffer(16);
#define PRINTREG(NAME)                                                                                                 \
  do {                                                                                                                 \
    printf(#NAME "\t%s\t", binfmt(z80->reg.main.NAME, 8, buf));                                                        \
    printf(#NAME "'\t%s\n", binfmt(z80->reg.alt.NAME, 8, buf));                                                        \
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
#define PRINTREG(WIDTH, REG1, REG2)                                                                                    \
  do {                                                                                                                 \
    printf(#REG1 "\t%s\t", binfmt(z80->reg.REG1, WIDTH, buf));                                                         \
    printf(#REG2 "\t%s\n", binfmt(z80->reg.REG2, WIDTH, buf));                                                         \
  } while (0)

  PRINTREG(8, i, r);
  PRINTREG(16, ix, iy);
  PRINTREG(16, sp, pc);

  free(buf);
#undef PRINTREG
}
