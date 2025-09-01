#include <z80/emulator.h>

/** Initialize WebAssembly module */
void init(void);

/** Reset the CPU */
void reset(void);

/** Get 8-bit register value
 *
 * @param r register name
 * @param alt access first (0) or second (1) set of registers
 */
zu8 get_register8(char r, int alt);

/** Set 8-bit register value
 *
 * @param r register name
 * @param v value
 * @param alt access first (0) or second (1) set of registers
 */
void set_register8(char r, zu8 v, int alt);

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
