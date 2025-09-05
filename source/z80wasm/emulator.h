#include <z80/emulator.h>

typedef enum {
  STATUS_OK = 0,
  STATUS_ERROR_NO_REGISTER = 1,
  STATUS_ERROR_DAA_INVALID_VALUE = -1,
  STATUS_ERROR_INVALID_OPCODE = -2
} status_type;

/** Initialize WebAssembly module */
void init(void);

/** Reset the CPU */
void reset(void);

/** Execute one instruction and set module status */
zi8 execute_instruction(void);

/** Allocate a buffer of size n
 *
 * Uses bump allocator under the hood and not intended to be used as a
 * replacement for `malloc`.
 *
 * @param n Size of the buffer
 * @returns Pointer to the buffer
 */
void* allocate(int n);

/** Get the status of the module
 *
 * Returns the status and sets the current status to STATUS_OK.
 *
 * @returns Status
 */
status_type get_status(void);

/** Get 8-bit register value
 *
 * @param r register name
 * @param alt access first (0) or second (1) set of registers
 */
zu8 get_register8(char const* r, int alt);

/** Set 8-bit register value
 *
 * @param r register name
 * @param v value
 * @param alt access first (0) or second (1) set of registers
 */
void set_register8(char const* r, zu8 v, int alt);

/** Get 16-bit register value
 *
 * @param r register name
 */
zu16 get_register16(char const* r);

/** Set 16-bit register value
 *
 * @param r register name
 * @param v value
 */
void set_register16(char const* r, zu16 v);
