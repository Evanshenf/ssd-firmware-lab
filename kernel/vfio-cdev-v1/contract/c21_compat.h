/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef FWLAB_C21_COMPAT_H
#define FWLAB_C21_COMPAT_H

#ifdef __KERNEL__
#include <linux/build_bug.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>

typedef u8 c21_u8;
typedef u16 c21_u16;
typedef u32 c21_u32;
typedef u64 c21_u64;
typedef s32 c21_s32;
#define C21_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t c21_u8;
typedef uint16_t c21_u16;
typedef uint32_t c21_u32;
typedef uint64_t c21_u64;
typedef int32_t c21_s32;
#define C21_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

#define C21_U64_MAX ((c21_u64)~(c21_u64)0)

#endif /* FWLAB_C21_COMPAT_H */
