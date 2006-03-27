/* fpu.c
 *
 * Actually, this file only contains the "ESCAPE" instruction, which handles
 * all FPU instructions by swallowing all ModR/M data and logging a little.
 *
 * Someday.
 *
 */

#include "vomit.h"
#include "debug.h"

void
_ESCAPE()
{
	byte rm = cpu_pfq_getbyte();
	(void) cpu_rmptr( rm, 16 );

	vlog( VM_CPUMSG, "%04X:%04X FPU escape via %02X /%u", BCS, BIP, cpu_opcode, rmreg( rm ));

#if 0
	/* 80286+: Coprocessor not available exception. */
	int_call( 7 );
#endif
}