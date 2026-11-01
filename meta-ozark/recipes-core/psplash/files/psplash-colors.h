/*
 * Ozark OS psplash colours.
 * The background must match the splash PNG's background (#142A1D) so the
 * logo blends into the rest of the screen at any resolution.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _HAVE_PSPLASH_COLORS_H
#define _HAVE_PSPLASH_COLORS_H

/* Deep ridge green #142A1D */
#define PSPLASH_BACKGROUND_COLOR 0x14,0x2a,0x1d

/* Cream #F1EBDD */
#define PSPLASH_TEXT_COLOR 0xf1,0xeb,0xdd

/* Trail orange #C8602B (progress bar is disabled, kept for completeness) */
#define PSPLASH_BAR_COLOR 0xc8,0x60,0x2b

/* Badge green #1F3B2D */
#define PSPLASH_BAR_BACKGROUND_COLOR 0x1f,0x3b,0x2d

#endif
