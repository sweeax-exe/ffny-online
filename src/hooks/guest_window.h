/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The contiguous window's guest range, as the toolkit built it (kernel.h's XBOX_CONTIG_BASE and
 * XBOX_CONTIG_SIZE; XBOXRECOMP_CONTIG_MB in CMakeLists.txt). The game's own heap is one block of it
 * (docs/research/rollback-state.md 1.3), so every check "is this a heap pointer" goes through here. */
#ifndef DEFJAM_GUEST_WINDOW_H
#define DEFJAM_GUEST_WINDOW_H
#include <stdint.h>
#ifndef XBOX_CONTIG_MB
#define XBOX_CONTIG_MB 64
#endif
#define GUEST_WINDOW_LO    0x80000000u
#define GUEST_WINDOW_BYTES ((uint32_t)(XBOX_CONTIG_MB) << 20)
#define GUEST_WINDOW_HI    (GUEST_WINDOW_LO + GUEST_WINDOW_BYTES)
#endif
