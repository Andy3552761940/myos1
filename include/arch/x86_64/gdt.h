#pragma once
#include <stdint.h>

/* Segment selectors */
#define GDT_SEL_KCODE 0x08
#define GDT_SEL_KDATA 0x10
#define GDT_SEL_UCODE 0x1B  /* index 3, RPL=3 */
#define GDT_SEL_UDATA 0x23  /* index 4, RPL=3 */
#define GDT_SEL_TSS   0x28  /* index 5 */

/* Initialize a full GDT and load a TSS (also sets IST1). */
void gdt_init(void);

/* Update kernel RSP0 used on privilege change (ring3->ring0). */
void tss_set_rsp0(uint64_t rsp0);
