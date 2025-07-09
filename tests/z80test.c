/* Z80e test suite.
 *
 * To start the emulator, create two binary files: memory file with the size >= 64kB
 * and the io file, and pass them to the executable: `./z80test memfile iofile`.
 *
 * Suite allows presetting registers before program execution by using -r option:
 * To modify the A register, specify `-ra=<hex-int>`, to modify the complementary
 * A register, specify `-ra_alt=<hex-int>`. `<hex-int>` follows the format accepted
 * by strtoul with base 16.
 *
 * Suite also allows setting Program Counter values on which it will emit register dumps.
 * To specify a dump point, pass `-dump=<hex-int>` option. It is allowed to specify
 * multiple points by passing several `-dump` options. Registers are printed when the PC
 * is greater than or equal to the dump point.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <z80e.h>

#include "utils/linkedlist.h"

typedef struct {
  z80e* z80;
  linkedlist* dump_points;
  char const* mem_filename;
  char const* io_filename;
  FILE* memfile;
  FILE* iofile;
} program_context;

void memwrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t memread(uint32_t addr, void* ctx);
void iowrite(uint32_t addr, uint8_t byte, void* ctx);
uint8_t ioread(uint32_t addr, void* ctx);

int startswith(char const* s1, char const* s2);
int parse_args(program_context* ctx, int argc, char** argv);
void print_usage(FILE* file);

char* alloc_binfmt_buffer(int max_bits);
char const* binfmt(uint32_t v, int width, char* buf);
void register_dump(z80e* z80);

int main(int argc, char* argv[]) {
  int ret = EXIT_SUCCESS;

  program_context progctx = {0};

  z80e_config cfg = {
      .memwrite = memwrite,
      .memread = memread,
      .iowrite = iowrite,
      .ioread = ioread,
      .ctx = &progctx,
  };

  z80e z80;
  z80e_init(&z80, &cfg);

  progctx.dump_points = ll_init();
  progctx.z80 = &z80;

  if (parse_args(&progctx, argc, argv) != 0) {
    print_usage(stderr);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  if ((progctx.memfile = fopen(progctx.mem_filename, "rb+")) == NULL) {
    fprintf(stderr, "cannot open file %s: %s\n", progctx.mem_filename, strerror(errno));
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  if ((progctx.iofile = fopen(progctx.io_filename, "rb+")) == NULL) {
    fprintf(stderr, "cannot open file %s: %s\n", progctx.io_filename, strerror(errno));
    free(progctx.memfile);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  while (!z80e_get_halt(&z80)) {
    ll_node* node = ll_first(progctx.dump_points);
    if (node != NULL && *(unsigned long*)ll_data(node) <= z80.reg.pc) {
      ll_pop_front_discard(progctx.dump_points);
      register_dump(&z80);
      printf("\n");
    }

    if (z80e_instruction(&z80) == Z80E_INVALID_OPCODE) {
      fprintf(stderr, "at 0x%04x: invalid instruction opcode\n", z80.reg.pc);
      ret = EXIT_FAILURE;
      goto cleanup_fds;
    }
  }

  register_dump(&z80);

cleanup_fds:
  fclose(progctx.memfile);
  fclose(progctx.iofile);

cleanup:
  ll_destroy(progctx.dump_points);

  return ret;
}

void memwrite(uint32_t addr, uint8_t byte, void* ctx) {
  program_context* c = ctx;
  uint8_t buf[1];
  buf[0] = byte;
  fseek(c->memfile, addr, SEEK_SET);
  fwrite(buf, 1, 1, c->memfile);
}

uint8_t memread(uint32_t addr, void* ctx) {
  program_context* c = ctx;
  uint8_t buf[1];
  fseek(c->memfile, addr, SEEK_SET);
  fread(buf, 1, 1, c->memfile);
  return buf[0];
}

void iowrite(uint32_t addr, uint8_t byte, void* ctx) {
  program_context* c = ctx;
  uint8_t buf[1];
  buf[0] = byte;
  fseek(c->iofile, addr, SEEK_SET);
  fwrite(buf, 1, 1, c->iofile);
}

uint8_t ioread(uint32_t addr, void* ctx) {
  program_context* c = ctx;
  uint8_t buf[1];
  fseek(c->iofile, addr, SEEK_SET);
  fread(buf, 1, 1, c->iofile);
  return buf[0];
}

int startswith(char const* s1, char const* s2) {
  assert(s1);
  assert(s2);

  size_t len1 = strlen(s1), len2 = strlen(s2);
  return len1 > len2 ? 0 : memcmp(s1, s2, len1) == 0;
}

int parse_args(program_context* ctx, int argc, char** argv) {
  assert(ctx);

  /* Discard the program path */
  --argc;
  ++argv;

#define PARSE_REG(_name, _dest, _bits)                                                                                 \
  if (startswith("-r", argv[0]) && startswith(_name, argv[0] + 2)) {                                                   \
    char* e = strchr(argv[0], '=');                                                                                    \
    if (e == NULL) {                                                                                                   \
      fprintf(stderr, "expected '=' after -r" _name);                                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
    char* last;                                                                                                        \
    unsigned long val = strtoul(argv[0] + 3 + strlen(_name), &last, 16);                                               \
    if (val > (1UL << _bits) - 1) {                                                                                    \
      fprintf(stderr, "expected %i bit integer: %s\n", _bits, argv[0]);                                                \
      return 1;                                                                                                        \
    }                                                                                                                  \
    if (*last != '\0' || errno) {                                                                                      \
      fprintf(stderr, "invalid base 16 integer value: %s", argv[0] + 3 + strlen(_name));                               \
      if (errno) {                                                                                                     \
        fprintf(stderr, ": %s\n", strerror(errno));                                                                    \
      } else {                                                                                                         \
        fprintf(stderr, ": %s\n", last);                                                                                         \
      }                                                                                                                \
      return 1;                                                                                                        \
    }                                                                                                                  \
    ctx->z80->_dest = val;                                                                                             \
  } else

#define PARSE_ARG(_name, _bits, _action)                                                                               \
  if (startswith(_name, argv[0] + 1)) {                                                                                \
    char* e = strchr(argv[0], '=');                                                                                    \
    if (e == NULL) {                                                                                                   \
      fprintf(stderr, "expected '=' after -" _name);                                                                   \
      return 1;                                                                                                        \
    }                                                                                                                  \
    char* last;                                                                                                        \
    unsigned long val = strtoul(argv[0] + 2 + strlen(_name), &last, 16);                                               \
    if (val > (1UL << _bits) - 1) {                                                                                    \
      fprintf(stderr, "expected %i bit integer: %s\n", _bits, argv[0]);                                                \
      return 1;                                                                                                        \
    }                                                                                                                  \
    if (*last != '\0' || errno) {                                                                                      \
      fprintf(stderr, "invalid base 16 integer value: %s", argv[0] + 3 + strlen(_name));                               \
      if (errno) {                                                                                                     \
        fprintf(stderr, ": %s\n", strerror(errno));                                                                    \
      } else {                                                                                                         \
        fprintf(stderr, "\n");                                                                                         \
      }                                                                                                                \
      return 1;                                                                                                        \
    }                                                                                                                  \
    _action;                                                                                                           \
  } else

#define PARSE_END()                                                                                                    \
  {                                                                                                                  \
    fprintf(stderr, "unrecognized argument: %s\n", argv[0]);                                                           \
    return 1;                                                                                                          \
  }

  int pos_arg_count = 0;
  while (argc != 0) {
    if ((*argv[0]) == '-') {
      PARSE_REG("a_alt", reg.alt.a, 8)
      PARSE_REG("b_alt", reg.alt.b, 8)
      PARSE_REG("c_alt", reg.alt.c, 8)
      PARSE_REG("d_alt", reg.alt.d, 8)
      PARSE_REG("e_alt", reg.alt.e, 8)
      PARSE_REG("f_alt", reg.alt.f, 8)
      PARSE_REG("h_alt", reg.alt.h, 8)
      PARSE_REG("l_alt", reg.alt.l, 8)
      PARSE_REG("a", reg.main.a, 8)
      PARSE_REG("b", reg.main.b, 8)
      PARSE_REG("c", reg.main.c, 8)
      PARSE_REG("d", reg.main.d, 8)
      PARSE_REG("e", reg.main.e, 8)
      PARSE_REG("f", reg.main.f, 8)
      PARSE_REG("h", reg.main.h, 8)
      PARSE_REG("l", reg.main.l, 8)
      PARSE_REG("ix", reg.ix, 16)
      PARSE_REG("iy", reg.iy, 16)
      PARSE_REG("i", reg.i, 8)
      PARSE_REG("r", reg.r, 8)
      PARSE_REG("sp", reg.sp, 16)
      PARSE_REG("pc", reg.pc, 16)

      PARSE_ARG("dump", 16, { ll_append(ctx->dump_points, &val, sizeof(val)); })
      /*
       * XXX: Add interrupt points. Not implemented yet.
       * PARSE_ARG("irq", { ll_append(ctx->irq_points, &val, sizeof(val)); });
       * PARSE_ARG("nmi", { ll_append(ctx->nmi_points, &val, sizeof(val)); });
       */
      PARSE_END();
    } else {
      switch (pos_arg_count) {
      case 0:
        ctx->mem_filename = argv[0];
        break;
      case 1:
        ctx->io_filename = argv[0];
        break;
      default:
        fprintf(stderr, "unexpected positional argument %s\n", argv[0]);
        return 1;
      }
      ++pos_arg_count;
    }
    --argc;
    ++argv;
  }

  if (pos_arg_count != 2) {
    fprintf(stderr, "expected 2 positional arguments\n");
    return 1;
  }

#undef PARSE_REG
#undef PARSE_ARG
#undef PARSE_END

  return 0;
}

void print_usage(FILE* file) { fprintf(file, "usage: z80test <memfile> <iofile> [-rR=HEX] [-dump=HEX]"); }

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
