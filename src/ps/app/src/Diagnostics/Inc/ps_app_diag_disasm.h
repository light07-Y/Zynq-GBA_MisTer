#ifndef PS_APP_DIAG_DISASM_H
#define PS_APP_DIAG_DISASM_H

#include "xil_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Disassemble ARM or Thumb code from PS DDR and print to UART.
 * gba_pc: the GBA CPU PC (0x08000000 for ROM, 0x03000000 for IWRAM, etc.)
 * num_lines: number of instructions to disassemble (default 16)
 *
 * ROM addresses (0x08000000–0x09FFFFFF) are read from PS DDR.
 * Non-ROM addresses (BIOS/IWRAM/WRAM) cannot be read — a note is printed.
 *
 * Returns number of lines actually printed. */
u32 PsAppDiag_DisasmAtPc(u32 gba_pc, u32 num_lines);

/* Disassemble ROM code around a list of GBA PCs seen during a hang,
 * deduplicating nearby addresses.  Called from hang snapshot. */
void PsAppDiag_DisasmHangPcs(const u32 *pc_list, u32 pc_count);

#ifdef __cplusplus
}
#endif

#endif
