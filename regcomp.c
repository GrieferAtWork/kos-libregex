/* Copyright (c) 2019-2025 Griefer@Work                                       *
 *                                                                            *
 * This software is provided 'as-is', without any express or implied          *
 * warranty. In no event will the authors be held liable for any damages      *
 * arising from the use of this software.                                     *
 *                                                                            *
 * Permission is granted to anyone to use this software for any purpose,      *
 * including commercial applications, and to alter it and redistribute it     *
 * freely, subject to the following restrictions:                             *
 *                                                                            *
 * 1. The origin of this software must not be misrepresented; you must not    *
 *    claim that you wrote the original software. If you use this software    *
 *    in a product, an acknowledgement (see the following) in the product     *
 *    documentation is required:                                              *
 *    Portions Copyright (c) 2019-2025 Griefer@Work                           *
 * 2. Altered source versions must be plainly marked as such, and must not be *
 *    misrepresented as being the original software.                          *
 * 3. This notice may not be removed or altered from any source distribution. *
 */
#ifndef GUARD_LIBREGEX_REGCOMP_C
#define GUARD_LIBREGEX_REGCOMP_C 1
#define _GNU_SOURCE 1
#define _KOS_SOURCE 1
#define LIBREGEX_WANT_PROTOTYPES

#include "api.h"
/**/

#ifndef LIBREGEX_NO_SYSTEM_INCLUDES
#include <hybrid/compiler.h>

#include <hybrid/align.h>
#include <hybrid/bitset.h>
#include <hybrid/minmax.h>
#include <hybrid/overflow.h>
#include <hybrid/unaligned.h>

#include <kos/types.h>

#include <assert.h>
#include <ctype.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unicode.h>

#include <libc/template/hex.h>
#include <libregex/regcomp.h>
#endif /* !LIBREGEX_NO_SYSTEM_INCLUDES */

#include "regcomp.h"
#include "regfast.h"
#include "regpeep.h"

#ifndef NDEBUG
#ifndef LIBREGEX_NO_SYSTEM_INCLUDES
#include <format-printer.h>
#include <inttypes.h>
#endif /* !LIBREGEX_NO_SYSTEM_INCLUDES */
#endif /* !NDEBUG */

/* Regex compile-time configuration */
#ifndef RE_COMP_MAXSIZE
#define RE_COMP_MAXSIZE 0x10000 /* 2^16 (hard limit on how large regex code blobs may get) */
#endif /* !RE_COMP_MAXSIZE */

/* Max length for alternation bytecode prefixes.
 * -> These are generated is it later becomes possible to produce per-byte fastmap
 *    offsets,  by replicating regex prefixes (`REOP_AT_*' and `REOP_GROUP_START')
 *    before each element of a top-level alternation.
 *    iow: "^(foo|bar)$" is compiled as "(^foo|^bar)$",
 *         with  a fast-map that dispatches "f" and "b"
 *         directly to the relevant alternation.
 * -> For this purpose, only generate up to `ALTERNATION_PREFIX_MAXLEN' bytes
 *    of prefix instructions before giving up and not repeating any prefixes. */
#ifndef ALTERNATION_PREFIX_MAXLEN
#define ALTERNATION_PREFIX_MAXLEN 16
#endif /* !ALTERNATION_PREFIX_MAXLEN */

/* Must # of  ASCII characters that  should appear in  the
 * operand of `REOP_[N]CONTAINS_UTF8', before the compiler
 * should produce a `REOP_[N]CS_UTF8'-sequence instead. */
#ifndef REOP_CONTAINS_UTF8_MAX_ASCII_COUNT
#define REOP_CONTAINS_UTF8_MAX_ASCII_COUNT 4
#endif /* !REOP_CONTAINS_UTF8_MAX_ASCII_COUNT */

/* Regex syntax test functions */
/*[[[deemon
local SYNTAX_OPTIONS = {
	"BACKSLASH_ESCAPE_IN_LISTS",
	"BK_PLUS_QM",
	"CHAR_CLASSES",
	"CONTEXT_INDEP_ANCHORS",
	"CONTEXT_INVALID_OPS",
	"DOT_NEWLINE",
	"DOT_NOT_NULL",
	"HAT_LISTS_NOT_NEWLINE",
	"INTERVALS",
	"LIMITED_OPS",
	"NEWLINE_ALT",
	"NO_BK_BRACES",
	"NO_BK_PARENS",
	"NO_BK_REFS",
	"NO_BK_VBAR",
	"NO_EMPTY_RANGES",
	"UNMATCHED_RIGHT_PAREN_ORD",
	"NO_POSIX_BACKTRACKING",
	"NO_GNU_OPS",
	"INVALID_INTERVAL_ORD",
	"ICASE",
	"CARET_ANCHORS_HERE",
	"CONTEXT_INVALID_DUP",
	"ANCHORS_IGNORE_EFLAGS",
	"NO_UTF8",
	"NO_KOS_OPS",
};
for (local opt: SYNTAX_OPTIONS) {
	print("#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_", opt);
	print("#define IF_", opt, "(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_", opt);
	print("#else /" "* LIBREGEX_CONSTANT__RE_SYNTAX_", opt, " *" "/");
	print("#define IF_", opt, "(syntax) (((syntax) & RE_SYNTAX_", opt, ") != 0)");
	print("#endif /" "* LIBREGEX_CONSTANT__RE_SYNTAX_", opt, " *" "/");
}
]]]*/
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_BACKSLASH_ESCAPE_IN_LISTS
#define IF_BACKSLASH_ESCAPE_IN_LISTS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_BACKSLASH_ESCAPE_IN_LISTS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_BACKSLASH_ESCAPE_IN_LISTS */
#define IF_BACKSLASH_ESCAPE_IN_LISTS(syntax) (((syntax) & RE_SYNTAX_BACKSLASH_ESCAPE_IN_LISTS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_BACKSLASH_ESCAPE_IN_LISTS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_BK_PLUS_QM
#define IF_BK_PLUS_QM(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_BK_PLUS_QM
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_BK_PLUS_QM */
#define IF_BK_PLUS_QM(syntax) (((syntax) & RE_SYNTAX_BK_PLUS_QM) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_BK_PLUS_QM */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_CHAR_CLASSES
#define IF_CHAR_CLASSES(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_CHAR_CLASSES
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_CHAR_CLASSES */
#define IF_CHAR_CLASSES(syntax) (((syntax) & RE_SYNTAX_CHAR_CLASSES) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_CHAR_CLASSES */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INDEP_ANCHORS
#define IF_CONTEXT_INDEP_ANCHORS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INDEP_ANCHORS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INDEP_ANCHORS */
#define IF_CONTEXT_INDEP_ANCHORS(syntax) (((syntax) & RE_SYNTAX_CONTEXT_INDEP_ANCHORS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INDEP_ANCHORS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_OPS
#define IF_CONTEXT_INVALID_OPS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_OPS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_OPS */
#define IF_CONTEXT_INVALID_OPS(syntax) (((syntax) & RE_SYNTAX_CONTEXT_INVALID_OPS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_OPS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NEWLINE
#define IF_DOT_NEWLINE(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NEWLINE
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NEWLINE */
#define IF_DOT_NEWLINE(syntax) (((syntax) & RE_SYNTAX_DOT_NEWLINE) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NEWLINE */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NOT_NULL
#define IF_DOT_NOT_NULL(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NOT_NULL
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NOT_NULL */
#define IF_DOT_NOT_NULL(syntax) (((syntax) & RE_SYNTAX_DOT_NOT_NULL) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_DOT_NOT_NULL */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_HAT_LISTS_NOT_NEWLINE
#define IF_HAT_LISTS_NOT_NEWLINE(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_HAT_LISTS_NOT_NEWLINE
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_HAT_LISTS_NOT_NEWLINE */
#define IF_HAT_LISTS_NOT_NEWLINE(syntax) (((syntax) & RE_SYNTAX_HAT_LISTS_NOT_NEWLINE) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_HAT_LISTS_NOT_NEWLINE */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_INTERVALS
#define IF_INTERVALS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_INTERVALS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_INTERVALS */
#define IF_INTERVALS(syntax) (((syntax) & RE_SYNTAX_INTERVALS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_INTERVALS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_LIMITED_OPS
#define IF_LIMITED_OPS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_LIMITED_OPS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_LIMITED_OPS */
#define IF_LIMITED_OPS(syntax) (((syntax) & RE_SYNTAX_LIMITED_OPS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_LIMITED_OPS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NEWLINE_ALT
#define IF_NEWLINE_ALT(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NEWLINE_ALT
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NEWLINE_ALT */
#define IF_NEWLINE_ALT(syntax) (((syntax) & RE_SYNTAX_NEWLINE_ALT) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NEWLINE_ALT */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_BRACES
#define IF_NO_BK_BRACES(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_BRACES
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_BRACES */
#define IF_NO_BK_BRACES(syntax) (((syntax) & RE_SYNTAX_NO_BK_BRACES) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_BRACES */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_PARENS
#define IF_NO_BK_PARENS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_PARENS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_PARENS */
#define IF_NO_BK_PARENS(syntax) (((syntax) & RE_SYNTAX_NO_BK_PARENS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_PARENS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_REFS
#define IF_NO_BK_REFS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_REFS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_REFS */
#define IF_NO_BK_REFS(syntax) (((syntax) & RE_SYNTAX_NO_BK_REFS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_REFS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_VBAR
#define IF_NO_BK_VBAR(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_VBAR
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_VBAR */
#define IF_NO_BK_VBAR(syntax) (((syntax) & RE_SYNTAX_NO_BK_VBAR) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_BK_VBAR */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_EMPTY_RANGES
#define IF_NO_EMPTY_RANGES(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_EMPTY_RANGES
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_EMPTY_RANGES */
#define IF_NO_EMPTY_RANGES(syntax) (((syntax) & RE_SYNTAX_NO_EMPTY_RANGES) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_EMPTY_RANGES */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD
#define IF_UNMATCHED_RIGHT_PAREN_ORD(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD */
#define IF_UNMATCHED_RIGHT_PAREN_ORD(syntax) (((syntax) & RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_POSIX_BACKTRACKING
#define IF_NO_POSIX_BACKTRACKING(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_POSIX_BACKTRACKING
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_POSIX_BACKTRACKING */
#define IF_NO_POSIX_BACKTRACKING(syntax) (((syntax) & RE_SYNTAX_NO_POSIX_BACKTRACKING) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_POSIX_BACKTRACKING */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_GNU_OPS
#define IF_NO_GNU_OPS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_GNU_OPS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_GNU_OPS */
#define IF_NO_GNU_OPS(syntax) (((syntax) & RE_SYNTAX_NO_GNU_OPS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_GNU_OPS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_INVALID_INTERVAL_ORD
#define IF_INVALID_INTERVAL_ORD(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_INVALID_INTERVAL_ORD
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_INVALID_INTERVAL_ORD */
#define IF_INVALID_INTERVAL_ORD(syntax) (((syntax) & RE_SYNTAX_INVALID_INTERVAL_ORD) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_INVALID_INTERVAL_ORD */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_ICASE
#define IF_ICASE(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_ICASE
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_ICASE */
#define IF_ICASE(syntax) (((syntax) & RE_SYNTAX_ICASE) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_ICASE */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_CARET_ANCHORS_HERE
#define IF_CARET_ANCHORS_HERE(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_CARET_ANCHORS_HERE
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_CARET_ANCHORS_HERE */
#define IF_CARET_ANCHORS_HERE(syntax) (((syntax) & RE_SYNTAX_CARET_ANCHORS_HERE) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_CARET_ANCHORS_HERE */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_DUP
#define IF_CONTEXT_INVALID_DUP(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_DUP
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_DUP */
#define IF_CONTEXT_INVALID_DUP(syntax) (((syntax) & RE_SYNTAX_CONTEXT_INVALID_DUP) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_CONTEXT_INVALID_DUP */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_ANCHORS_IGNORE_EFLAGS
#define IF_ANCHORS_IGNORE_EFLAGS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_ANCHORS_IGNORE_EFLAGS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_ANCHORS_IGNORE_EFLAGS */
#define IF_ANCHORS_IGNORE_EFLAGS(syntax) (((syntax) & RE_SYNTAX_ANCHORS_IGNORE_EFLAGS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_ANCHORS_IGNORE_EFLAGS */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_UTF8
#define IF_NO_UTF8(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_UTF8
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_UTF8 */
#define IF_NO_UTF8(syntax) (((syntax) & RE_SYNTAX_NO_UTF8) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_UTF8 */
#ifdef LIBREGEX_CONSTANT__RE_SYNTAX_NO_KOS_OPS
#define IF_NO_KOS_OPS(syntax) LIBREGEX_CONSTANT__RE_SYNTAX_NO_KOS_OPS
#else /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_KOS_OPS */
#define IF_NO_KOS_OPS(syntax) (((syntax) & RE_SYNTAX_NO_KOS_OPS) != 0)
#endif /* LIBREGEX_CONSTANT__RE_SYNTAX_NO_KOS_OPS */
/*[[[end]]]*/

/* TODO: Generic more efficient code based on `LIBREGEX_CONSTANT__*' macros.
 *       Depending on `LIBREGEX_CONSTANT__*' macros, we're even able to  get
 *       rid of certain opcodes! */


#ifdef _MSC_VER
#pragma warning(disable: 4127) /* "warning C4127: conditional expression is constant" */
#endif /* _MSC_VER */

DECL_BEGIN

/* TODO: Implement as many extensions from "https://www.unicode.org/reports/tr18/" as possible */

#undef re_parser_yield
#define re_parser_yield(self) libre_parser_yield(self)

#if !defined(NDEBUG) && !defined(NDEBUG_FINI)
#define DBG_memset(p, c, n) memset(p, c, n)
#else /* !NDEBUG && !NDEBUG_FINI */
#define DBG_memset(p, c, n) (void)0
#endif /* NDEBUG || NDEBUG_FINI */

#define delta16_get(p)    ((int16_t)UNALIGNED_GET16(p))
#define delta16_set(p, v) UNALIGNED_SET16(p, (uint16_t)(int16_t)(v))

#define tswap(T, a, b)   \
	do {                 \
		T _temp = (b);   \
		(b)     = (a);   \
		(a)     = _temp; \
	}	__WHILE0



/************************************************************************/
/* ASCII character trait flags (s.a. `/kos/kos/include/bits/crt/ctype.h') */
/************************************************************************/
#define CTYPE_C_FLAG_CNTRL  0x01
#define CTYPE_C_FLAG_SPACE  0x02
#define CTYPE_C_FLAG_LOWER  0x04
#define CTYPE_C_FLAG_UPPER  0x08
#define CTYPE_C_FLAG_ALPHA  0x0c
#define CTYPE_C_FLAG_DIGIT  0x10
#define CTYPE_C_FLAG_XDIGIT 0x30
#define CTYPE_C_FLAG_ALNUM  0x1c
#define CTYPE_C_FLAG_PUNCT  0x40
#define CTYPE_C_FLAG_GRAPH  0x5c
#define CTYPE_C_FLAG_PRINT  0xdc
#ifdef LIBREGEX_DEFINE___CTYPE_C_FLAGS
#undef __ctype_C_flags
#define __ctype_C_flags libregex___ctype_C_flags
/* NOTE: "128", because we only ever test in the ASCII area */
PRIVATE uint8_t const libregex___ctype_C_flags[128] = {
	0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x3,0x3,0x3,0x3,0x3,0x1,0x1,
	0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x1,
	0x82,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x28,0x28,0x28,0x28,0x28,0x28,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,
	0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x40,0x40,0x40,0x40,0x40,
	0x40,0x24,0x24,0x24,0x24,0x24,0x24,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,
	0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x40,0x40,0x40,0x40,0x1
};
#endif /* !LIBREGEX_DEFINE___CTYPE_C_FLAGS */

#define ASCII_ISSPACE(c) ((c) == 0x20 || ((c) >= 0x09 && (c) <= 0x0d))
#define ASCII_ISDIGIT(c) ((c) >= '0' && (c) <= '9')



/* Mapping from `RECS_ISxxx - RECS_ISX_MIN' to `CTYPE_C_FLAG_*'.
 *
 * Those cases where mask exists encode `0' as mask, causing the
 * handler to do a custom encode below. */
PRIVATE uint8_t const ctype_c_trait_masks[] = {
#ifdef __GNUC__
#define DEF_CTYPE_TRAIT_MASK(opcode, mask) [((opcode) - RECS_ISX_MIN)] = mask
#else /* __GNUC__ */
#define DEF_CTYPE_TRAIT_MASK(opcode, mask) /*[((opcode) - RECS_ISX_MIN)] = */mask
#endif /* !__GNUC__ */
	DEF_CTYPE_TRAIT_MASK(RECS_ISCNTRL, CTYPE_C_FLAG_CNTRL),
	DEF_CTYPE_TRAIT_MASK(RECS_ISSPACE, CTYPE_C_FLAG_SPACE),
	DEF_CTYPE_TRAIT_MASK(RECS_ISUPPER, CTYPE_C_FLAG_UPPER),
	DEF_CTYPE_TRAIT_MASK(RECS_ISLOWER, CTYPE_C_FLAG_LOWER),
	DEF_CTYPE_TRAIT_MASK(RECS_ISALPHA, CTYPE_C_FLAG_ALPHA),
	DEF_CTYPE_TRAIT_MASK(RECS_ISDIGIT, CTYPE_C_FLAG_DIGIT),
	DEF_CTYPE_TRAIT_MASK(RECS_ISXDIGIT, CTYPE_C_FLAG_XDIGIT),
	DEF_CTYPE_TRAIT_MASK(RECS_ISALNUM, CTYPE_C_FLAG_ALNUM),
	DEF_CTYPE_TRAIT_MASK(RECS_ISPUNCT, CTYPE_C_FLAG_PUNCT),
	DEF_CTYPE_TRAIT_MASK(RECS_ISGRAPH, CTYPE_C_FLAG_GRAPH),
	DEF_CTYPE_TRAIT_MASK(RECS_ISPRINT, CTYPE_C_FLAG_PRINT),
	DEF_CTYPE_TRAIT_MASK(RECS_ISBLANK, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISSYMSTRT, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISSYMCONT, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISTAB, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISWHITE, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISEMPTY, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISLF, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISHEX, 0),
	DEF_CTYPE_TRAIT_MASK(RECS_ISTITLE, CTYPE_C_FLAG_UPPER),
	DEF_CTYPE_TRAIT_MASK(RECS_ISNUMERIC, CTYPE_C_FLAG_DIGIT),
#undef DEF_CTYPE_TRAIT_MASK
};




/************************************************************************/
/* Collating character names                                            */
/************************************************************************/
struct collating_char {
	char cc_name[23]; /* Collating char name */
	char cc_char;     /* ASCII character */
};

/* Collating character names from the POSIX locale (NOTE: must be sorted by `cc_name'). */
PRIVATE struct collating_char const posix_locale_cchars[] = {
#define CCHAR(name, ascii_ch) { name, ascii_ch }
	{ "ACK", /*                 */ 0x06 },
	{ "BEL", /*                 */ 0x07 },
	{ "BS", /*                  */ 0x08 },
	{ "CAN", /*                 */ 0x18 },
	{ "CR", /*                  */ 0x0d },
	{ "DC1", /*                 */ 0x11 },
	{ "DC2", /*                 */ 0x12 },
	{ "DC3", /*                 */ 0x13 },
	{ "DC4", /*                 */ 0x14 },
	{ "DEL", /*                 */ 0x7f },
	{ "DLE", /*                 */ 0x10 },
	{ "EM", /*                  */ 0x19 },
	{ "ENQ", /*                 */ 0x05 },
	{ "EOT", /*                 */ 0x04 },
	{ "ESC", /*                 */ 0x1b },
	{ "ETB", /*                 */ 0x17 },
	{ "ETX", /*                 */ 0x03 },
	{ "FF", /*                  */ 0x0c },
	{ "FS", /*                  */ 0x1c },
	{ "GS", /*                  */ 0x1d },
	{ "HT", /*                  */ 0x09 },
	{ "IS1", /*                 */ 0x1f },
	{ "IS2", /*                 */ 0x1e },
	{ "IS3", /*                 */ 0x1d },
	{ "IS4", /*                 */ 0x1c },
	{ "LF", /*                  */ 0x0a },
	{ "NAK", /*                 */ 0x15 },
	{ "NUL", /*                 */ 0x01 },
	{ "RS", /*                  */ 0x1e },
	{ "SI", /*                  */ 0x0f },
	{ "SO", /*                  */ 0x0e },
	{ "SOH", /*                 */ 0x01 },
	{ "STX", /*                 */ 0x02 },
	{ "SUB", /*                 */ 0x1a },
	{ "SYN", /*                 */ 0x16 },
	{ "US", /*                  */ 0x1f },
	{ "VT", /*                  */ 0x0b },
	{ "alert", /*               */ 0x07 },
	{ "ampersand", /*           */ '&' },
	{ "apostrophe", /*          */ '\'' },
	{ "asterisk", /*            */ '*' },
	{ "backslash", /*           */ '\\' },
	{ "backspace", /*           */ 0x08 },
	{ "carriage-return", /*     */ 0x0d },
	{ "circumflex", /*          */ '^' },
	{ "circumflex-accent", /*   */ '^' },
	{ "colon", /*               */ ':' },
	{ "comma", /*               */ ',' },
	{ "commercial-at", /*       */ '@' },
	{ "dollar-sign", /*         */ '$' },
	{ "eight", /*               */ '8' },
	{ "equals-sign", /*         */ '=' },
	{ "exclamation-mark", /*    */ '!' },
	{ "five", /*                */ '5' },
	{ "form-feed", /*           */ 0x0c },
	{ "four", /*                */ '4' },
	{ "full-stop", /*           */ '.' },
	{ "grave-accent", /*        */ '`' },
	{ "greater-than-sign", /*   */ '>' },
	{ "hyphen", /*              */ '-' },
	{ "hyphen-minus", /*        */ '-' },
	{ "left-brace", /*          */ '{' },
	{ "left-curly-bracket", /*  */ '{' },
	{ "left-parenthesis", /*    */ '(' },
	{ "left-square-bracket", /* */ '[' },
	{ "less-than-sign", /*      */ '<' },
	{ "low-line", /*            */ '_' },
	{ "newline", /*             */ 0x0a },
	{ "nine", /*                */ '9' },
	{ "number-sign", /*         */ '#' },
	{ "one", /*                 */ '1' },
	{ "percent-sign", /*        */ '%' },
	{ "period", /*              */ '.' },
	{ "plus-sign", /*           */ '+' },
	{ "question-mark", /*       */ '?' },
	{ "quotation-mark", /*      */ '"' },
	{ "reverse-solidus", /*     */ '\\' },
	{ "right-brace", /*         */ '}' },
	{ "right-curly-bracket", /* */ '}' },
	{ "right-parenthesis", /*   */ ')' },
	{ "right-square-bracket", /**/ ']' },
	{ "semicolon", /*           */ ';' },
	{ "seven", /*               */ '7' },
	{ "six", /*                 */ '6' },
	{ "slash", /*               */ '/' },
	{ "solidus", /*             */ '/' },
	{ "space", /*               */ ' ' },
	{ "tab", /*                 */ 0x09 },
	{ "three", /*               */ '3' },
	{ "tilde", /*               */ '~' },
	{ "two", /*                 */ '2' },
	{ "underscore", /*          */ '_' },
	{ "vertical-line", /*       */ '|' },
	{ "vertical-tab", /*        */ 0x0b },
	{ "zero", /*                */ '0' },
#undef CCHAR
};





/************************************************************************/
/* RE PARSER                                                            */
/************************************************************************/

struct re_interval {
	uint8_t ri_min;  /* Interval lower bound */
	uint8_t ri_max;  /* [valid_if(!ri_many)] Interval upper bound */
	bool    ri_many; /* true if `> 1' is accepted (encountered '*' or '+') */
};

/* Parse a regex interval expression, with `*p_pattern' pointing AFTER the leading '{'
 * @return: true:  Interval is OK
 * @return: false: Interval is malformed */
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC parse_interval)(char const **__restrict p_pattern, uintptr_t syntax,
                               struct re_interval *__restrict result) {
	/* Parse an interval */
	char const *pattern = *p_pattern;
	uint32_t interval_min, interval_max;
	(void)syntax;
	if (!ASCII_ISDIGIT(*pattern))
		return false;
	interval_min = *pattern - '0';
	++pattern;
	while (ASCII_ISDIGIT(*pattern)) {
		if (OVERFLOW_UMUL(interval_min, 10, &interval_min))
			return false;
		if (OVERFLOW_UADD(interval_min, (uint8_t)(*pattern - '0'), &interval_min))
			return false;
		++pattern;
	}
	result->ri_many = false;
	interval_max    = interval_min;
	if (*pattern == ',') {
		++pattern;
		if (IF_NO_BK_BRACES(syntax)
		    ? (pattern[0] == '}')
		    : (pattern[0] == '\\' && pattern[1] == '}')) {
			result->ri_many = true;
		} else {
			if (!ASCII_ISDIGIT(*pattern))
				return false;
			interval_max = *pattern - '0';
			++pattern;
			while (ASCII_ISDIGIT(*pattern)) {
				if (OVERFLOW_UMUL(interval_max, 10, &interval_max))
					return false;
				if (OVERFLOW_UADD(interval_max, (uint8_t)(*pattern - '0'), &interval_max))
					return false;
				++pattern;
			}
		}
	}
	if (IF_NO_BK_BRACES(syntax)) {
		if (pattern[0] != '}')
			return false;
		++pattern;
	} else {
		if (pattern[0] != '\\')
			return false;
		if (pattern[1] != '}')
			return false;
		pattern += 2;
	}
	if unlikely(interval_min > interval_max)
		return false;
	if unlikely(interval_max > UINT8_MAX)
		return false;
	*p_pattern = pattern;
	result->ri_min = (uint8_t)interval_min;
	result->ri_max = (uint8_t)interval_max;
	return true;
}

/* Check if `p' (which must point after the leading '{') is a valid interval */
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC is_a_valid_interval)(char const *__restrict p, uintptr_t syntax) {
	struct re_interval interval;
	return parse_interval((char const **)&p, syntax, &interval);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4701) /* Bogus warning */
#endif /* _MSC_VER */

/* Parse and yield the next regex-token pointed-to by `self->rep_pos'
 * @return: * : A unicode character, or one of `RE_TOKEN_*' */
INTERN WUNUSED NONNULL((1)) re_token_t
NOTHROW_NCX(CC libre_parser_yield)(struct re_parser *__restrict self) {
	unsigned char ch;
	ch = (unsigned char)*self->rep_pos++;
	switch (ch) {

	case '\0':
		assert(self->rep_pos - 1 <= self->rep_end);
		if (self->rep_pos - 1 >= self->rep_end) {
			--self->rep_pos;
			return RE_TOKEN_EOF;
		}
		break; /* Encode as a literal NUL-byte */

	case '.':
		return RE_TOKEN_ANY;

	case '[':
		/* TODO: A single-element charset (e.g. "[$]") should be parsed as a literal.
		 *       That way, the compiler can directly merge it with adjacent literals! */
		return RE_TOKEN_STARTSET;

	case '{':
		if (IF_INTERVALS(self->rep_syntax) && IF_NO_BK_BRACES(self->rep_syntax)) {
			if (!IF_INVALID_INTERVAL_ORD(self->rep_syntax))
				return RE_TOKEN_STARTINTERVAL; /* '{' is always an interval */
			if (is_a_valid_interval(self->rep_pos, self->rep_syntax))
				return RE_TOKEN_STARTINTERVAL; /* '{' followed by a valid interval is OK */
		}
		break;

	case '(':
		if (IF_NO_BK_PARENS(self->rep_syntax))
			return RE_TOKEN_STARTGROUP;
		break;

	case ')':
		if (IF_NO_BK_PARENS(self->rep_syntax) && !IF_UNMATCHED_RIGHT_PAREN_ORD(self->rep_syntax))
			return RE_TOKEN_ENDGROUP;
		break;

	case '^':
		if (IF_CONTEXT_INDEP_ANCHORS(self->rep_syntax) ||
		    IF_CARET_ANCHORS_HERE(self->rep_syntax))
			return RE_TOKEN_AT_SOL;
		if (self->rep_pos == self->rep_pat + 1)
			return RE_TOKEN_AT_SOL; /* Always special at start of pattern */
		if (self->rep_pos[-1] == '(') {
			/* Also special if following a non-escaped open-group */
			bool is_escaped  = false;
			char const *iter = self->rep_pos - 2;
			while (iter >= self->rep_pat && *iter == '\\') {
				--iter;
				is_escaped = !is_escaped;
			}
			if (!IF_NO_BK_PARENS(self->rep_syntax))
				is_escaped = !is_escaped; /* Invert meaning of escaped vs. non-escaped */
			if (!is_escaped) {
				/* '^' is following a non-escaped '(' -> it's an AT-marker! */
				return RE_TOKEN_AT_SOL;
			}
		}
		break;

	case '$':
		if (IF_CONTEXT_INDEP_ANCHORS(self->rep_syntax))
			return RE_TOKEN_AT_EOL;
		if (self->rep_pos[0] == '\0' && self->rep_pos >= self->rep_end)
			return RE_TOKEN_AT_EOL; /* Always special at end of pattern */
		if ((self->rep_pos[0] == ')') && IF_NO_BK_PARENS(self->rep_syntax))
			return RE_TOKEN_AT_EOL; /* Always special before group-close */
		if ((self->rep_pos[0] == '\\' && self->rep_pos[1] == ')') && !IF_NO_BK_PARENS(self->rep_syntax))
			return RE_TOKEN_AT_EOL; /* Always special before group-close */
		break;

	case '+':
		if (IF_LIMITED_OPS(self->rep_syntax))
			break; /* "+" operator is disabled */
		if (!IF_BK_PLUS_QM(self->rep_syntax))
			return RE_TOKEN_PLUS; /* "+" is an operator */
		break;

	case '?':
		if (IF_LIMITED_OPS(self->rep_syntax))
			break; /* "?" operator is disabled */
		if (!IF_BK_PLUS_QM(self->rep_syntax))
			return RE_TOKEN_QMARK; /* "?" is an operator */
		break;

	case '*':
		return RE_TOKEN_STAR;

	case '\n':
		if (!IF_NEWLINE_ALT(self->rep_syntax))
			break;
		ATTR_FALLTHROUGH
	case '|':
		if (IF_LIMITED_OPS(self->rep_syntax))
			break; /* "|" operator is disabled */
		if (IF_NO_BK_VBAR(self->rep_syntax))
			return RE_TOKEN_ALTERNATION; /* "|" is an operator */
		break;

	case '\\':
		ch = (unsigned char)*self->rep_pos++;
		switch (ch) {

		case '\0':
			assert(self->rep_pos - 1 <= self->rep_end);
			if (self->rep_pos - 1 >= self->rep_end) {
				self->rep_pos -= 2; /* Keep on repeating this token! */
				return RE_TOKEN_UNMATCHED_BK;
			}
			goto default_escaped_char;

		case '{':
			if (IF_INTERVALS(self->rep_syntax) && !IF_NO_BK_BRACES(self->rep_syntax)) {
				if (!IF_INVALID_INTERVAL_ORD(self->rep_syntax))
					return RE_TOKEN_STARTINTERVAL;
				if (is_a_valid_interval(self->rep_pos, self->rep_syntax))
					return RE_TOKEN_STARTINTERVAL; /* '{' followed by a valid interval is OK */
			}
			break;

		case '(':
			if (!IF_NO_BK_PARENS(self->rep_syntax))
				return RE_TOKEN_STARTGROUP;
			break;

		case ')':
			if (!IF_NO_BK_PARENS(self->rep_syntax) && !IF_UNMATCHED_RIGHT_PAREN_ORD(self->rep_syntax))
				return RE_TOKEN_ENDGROUP;
			break;

		case '+':
			if (IF_LIMITED_OPS(self->rep_syntax))
				break; /* "+" operator is disabled */
			if (IF_BK_PLUS_QM(self->rep_syntax))
				return RE_TOKEN_PLUS; /* "\+" is an operator */
			break;

		case '?':
			if (IF_LIMITED_OPS(self->rep_syntax))
				break; /* "?" operator is disabled */
			if (IF_BK_PLUS_QM(self->rep_syntax))
				return RE_TOKEN_QMARK; /* "\?" is an operator */
			break;

		case '|':
			if (IF_LIMITED_OPS(self->rep_syntax))
				break; /* "|" operator is disabled */
			if (!IF_NO_BK_VBAR(self->rep_syntax))
				return RE_TOKEN_ALTERNATION; /* "\|" is an operator */
			break;

		case 'w':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_BK_w;
			break;

		case 'W':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_BK_W;
			break;

		case 's':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_BK_s;
			break;

		case 'S':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_BK_S;
			break;

		case 'd':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_BK_d;
			break;

		case 'D':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_BK_D;
			break;

		case 'n':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_BK_n;
			break;

		case 'N':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_BK_N;
			break;

		case '`':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_SOI;
			break;

		case '\'':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_EOI;
			break;

		case 'b':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_WOB;
			break;

		case 'B':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_WOB_NOT;
			break;

		case '<':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_SOW;
			break;

		case '>':
			if (!IF_NO_GNU_OPS(self->rep_syntax))
				return RE_TOKEN_AT_EOW;
			break;

		case '_':
			if (*self->rep_pos == '<') {
				++self->rep_pos;
				return RE_TOKEN_AT_SOS;
			}
			if (*self->rep_pos == '>') {
				++self->rep_pos;
				return RE_TOKEN_AT_EOS;
			}
			break;

		case 'A':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_AT_SOI;
			break;

		case 'Z':
			if (!IF_NO_KOS_OPS(self->rep_syntax))
				return RE_TOKEN_AT_EOI;
			break;

		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (!IF_NO_BK_REFS(self->rep_syntax))
				return RE_TOKEN_BKREF_1 + (ch - '1');
			break;

		case '0': {
			uint32_t ord;
			unsigned int i, ndigits;
			if (IF_NO_KOS_OPS(self->rep_syntax))
				break;
			ord     = 0;
			ndigits = 3;
			for (i = 0; i < ndigits; ++i, ++self->rep_pos) {
				uint8_t nibble;
				char octchar = *self->rep_pos;
				if (octchar >= '0' && octchar <= '7') {
					nibble = (uint8_t)(octchar - '0');
				} else {
					break;
				}
				ord <<= 3;
				ord |= nibble;
			}
			if (ord >= 0x80) {
				ord -= 0x80;
				ord += RE_TOKEN_BYTE80h_MIN;
			}
			return ord;
		}	break;

		case 'u': /* TODO: Support for unicode's "\u{ABC DEF 123}" -> "\u0ABC\u0DEF\u0123" */
		case 'U':
			if (self->rep_syntax & RE_SYNTAX_NO_UTF8)
				break;
			ATTR_FALLTHROUGH
		case 'x': {
			uint32_t ord;
			unsigned int i, ndigits;
			if (IF_NO_KOS_OPS(self->rep_syntax))
				break;
			ndigits = ch == 'U' ? 8 : ch == 'u' ? 4 : 2;
			ord     = 0;
			for (i = 0; i < ndigits; ++i, ++self->rep_pos) {
				uint8_t nibble;
				char hexchar = *self->rep_pos;
				if (!__libc_hex2int(hexchar, &nibble)) {
					self->rep_pos -= i;
					goto default_escaped_char;
				}
				ord <<= 4;
				ord |= nibble;
			}
			if unlikely(ord >= RE_TOKEN_BASE) {
				/* Guard against illegal unicode characters. */
				self->rep_pos -= ndigits;
				goto default_escaped_char;
			}
			if (ch == 'x') {
				/* Special case: This one's 80h-FFh must be encoded as `RE_TOKEN_BYTE80h_MIN',
				 *               as it's not supposed to  match U+0080-U+00FF, but rather  the
				 *               raw bytes 80h-FFh. */
				if (ord >= 0x80) {
					ord -= 0x80;
					ord += RE_TOKEN_BYTE80h_MIN;
				}
			}
			return ord;
		}	break;

		default:
			if (ch >= 0x80 /*&& ch <= 0xff*/)
				goto handle_utf8;
default_escaped_char:
			break;
		}
		break;

	default:
		if (ch >= 0x80 /*&& ch <= 0xff*/) {
			char32_t uni;
			uint8_t i, seqlen;
handle_utf8:
			if (self->rep_syntax & RE_SYNTAX_NO_UTF8) {
				/* Always emit byte-literals */
				return RE_TOKEN_BYTE80h_MIN + (ch - 0x80);
			}
			--self->rep_pos;
			/* Make sure that `unicode_readutf8(3)' won't access out-of-bounds memory */
			seqlen = unicode_utf8seqlen[ch];
			for (i = 0; i < seqlen; ++i) {
				if (self->rep_pos[i] == '\0' && (self->rep_pos + i) >= self->rep_end)
					return RE_TOKEN_ILLSEQ;
			}
			uni = unicode_readutf8(&self->rep_pos);
			if unlikely(uni >= RE_TOKEN_BASE) {
				self->rep_pos -= seqlen;
				return RE_TOKEN_ILLSEQ;
			}
			return uni;
		}
		break;
	}
	return ch; /* Default case: match a literal */
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif /* _MSC_VER */





/************************************************************************/
/* RE COMPILER                                                          */
/************************************************************************/
PRIVATE NONNULL((1)) bool
NOTHROW_NCX(CC re_compiler_require)(struct re_compiler *__restrict self,
                                    size_t num_bytes) {
	size_t avail;
	avail = (size_t)(self->rec_cend - self->rec_cpos);
	if (num_bytes > avail) {
		/* Must allocate more memory. */
		byte_t *old_base, *new_base;
		size_t old_size, new_minsize, new_size;
		old_base    = self->rec_cbase;
		old_size    = (size_t)(self->rec_cend - old_base);
		new_minsize = old_size - avail + num_bytes;
		assert(new_minsize == (size_t)(self->rec_cpos - self->rec_cbase) + num_bytes);
		new_size = old_size;
		while (new_size < new_minsize)
			new_size = (new_size << 1) | 1;
		++new_size;
		if unlikely(new_size > RE_COMP_MAXSIZE) {
			if (new_minsize > RE_COMP_MAXSIZE) {
				/* Allocate memory up until the maximum allowed (allowing
				 * our  caller  to replace  `RE_ESPACE'  with `RE_ESIZE') */
				re_compiler_require(self, RE_COMP_MAXSIZE - old_size);
				return false; /* Not allowed to allocate that much :( */
			}
			new_size = RE_COMP_MAXSIZE;
		}
		new_base = (byte_t *)realloc(old_base, new_size);
		if unlikely(!new_base) {
			new_size = new_minsize;
			new_base = (byte_t *)realloc(old_base, new_size);
			if unlikely(!new_base)
				return false; /* Unable to reallocate that much :( */
		}
		self->rec_cbase = new_base;
		self->rec_cend  = new_base + new_size;
		if (new_base != old_base) {
			/* Update pointers for the the block currently being compiled. */
			__pragma_GCC_diagnostic_push_ignored(Wuse_after_free) /* No: this use of `old_base' after free is OK! */
			ptrdiff_t delta = new_base - old_base;
			__pragma_GCC_diagnostic_pop_ignored(Wuse_after_free)
			self->rec_estart += delta;
			self->rec_cpos += delta;
		}
	}
	return true;
}


/* Append a single byte of code at the current buffer position. */
#define re_compiler_putc(self, b)        \
	((self)->rec_cpos < (self)->rec_cend \
	 ? (*(self)->rec_cpos++ = (b), true) \
	 : likely(_re_compiler_putc(self, b)))
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC _re_compiler_putc)(struct re_compiler *__restrict self,
                                  byte_t b) {
	assert(self->rec_cpos == self->rec_cend);
	if likely(re_compiler_require(self, 1)) {
		*self->rec_cpos++ = b;
		return true;
	}
	return false;
}

PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_compiler_putn)(struct re_compiler *__restrict self,
                                 void const *p, size_t n) {
	if likely(re_compiler_require(self, n)) {
		self->rec_cpos = (byte_t *)mempcpy(self->rec_cpos, p, n);
		return true;
	}
	return false;
}

/* Return a pointer to the start of the utf-8 character that contains `cptr' */
PRIVATE ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) char *
NOTHROW_NCX(CC utf8_baseptr)(char const *__restrict cptr) {
	while (unicode_utf8seqlen[(unsigned char)*cptr] == 0)
		--cptr; /* Seek backwards until we hit a non-follow-up byte. */
	return (char *)cptr;
}

/* Same  as   `unicode_readutf8(3)',   but   don't   advance   `cptr'
 * It is assumed that `cptr' points to the start of a utf-8 sequence. */
PRIVATE ATTR_PURE WUNUSED NONNULL((1)) char32_t
NOTHROW_NCX(CC utf8_charat)(char const *__restrict cptr) {
	return unicode_readutf8((char const **)&cptr);
}

/* Skip over `n' utf-8 characters. */
PRIVATE ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) char *
NOTHROW_NCX(CC utf8_skipn)(char const *__restrict cptr, size_t n) {
	for (; n; --n)
		cptr += unicode_utf8seqlen[(unsigned char)*cptr];
	return (char *)cptr;
}



/* Return a pointer to the next instruction */
INTERN ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) byte_t *
NOTHROW_NCX(CC libre_opcode_next)(byte_t const *__restrict p_instr) {
	byte_t opcode = *p_instr++;
	switch (opcode) {

	case REOP_EXACT:
	case REOP_EXACT_ASCII_ICASE: {
		byte_t length;
		length = *p_instr++;
		assert(length >= 2);
		p_instr += length;
	}	break;

	case REOP_EXACT_UTF8_ICASE:
	case REOP_CONTAINS_UTF8:
	case REOP_NCONTAINS_UTF8: {
		byte_t count;
		count = *p_instr++;
		assert(count >= 1);
		do {
			p_instr += unicode_utf8seqlen[*p_instr];
		} while (--count);
	}	break;

	case REOP_CS_BYTE:
	case REOP_CS_UTF8:
	case REOP_NCS_UTF8: {
		byte_t cs_opcode;
		while ((cs_opcode = *p_instr++) != RECS_DONE) {
			switch (cs_opcode) {
			case_RECS_BITSET_MIN_to_MAX_BYTE:
				if (cs_opcode <= RECS_BITSET_MAX_UTF8 || opcode == REOP_CS_BYTE)
					p_instr += RECS_BITSET_GETBYTES(cs_opcode);
				break;
			case RECS_CHAR:
				if (opcode == REOP_CS_BYTE) {
					p_instr += 1;
				} else {
					p_instr += unicode_utf8seqlen[*p_instr];
				}
				break;
			case RECS_CHAR2:
			case RECS_RANGE:
			case RECS_RANGE_ICASE:
				if (opcode == REOP_CS_BYTE) {
					p_instr += 2;
				} else {
					p_instr += unicode_utf8seqlen[*p_instr];
					p_instr += unicode_utf8seqlen[*p_instr];
				}
				break;
			case RECS_CONTAINS: {
				byte_t count = *p_instr++;
				assert(count >= 1);
				if (opcode == REOP_CS_BYTE) {
					p_instr += count;
				} else {
					do {
						p_instr += unicode_utf8seqlen[*p_instr];
					} while (--count);
				}
			}	break;
			default: break;
			}
		}
	}	break;

	case REOP_BYTE:
	case REOP_NBYTE:
	case REOP_GROUP_MATCH:
	case_REOP_GROUP_MATCH_JMIN_to_JMAX:
	case REOP_GROUP_START:
	case REOP_GROUP_END:
	case_REOP_GROUP_END_JMIN_to_JMAX:
		p_instr += 1;
		break;

	case REOP_BYTE2:
	case REOP_NBYTE2:
	case REOP_RANGE:
	case REOP_NRANGE:
	case REOP_JMP:
	case REOP_JMP_ONFAIL:
	case REOP_JMP_AND_RETURN_ONFAIL:
	case REOP_SETVAR:
	case REOP_MAYBE_POP_ONFAIL:
	case REOP_POP_ONFAIL_AT:
	case REOP_JMP_ONFAIL_DUMMY_AT:
		p_instr += 2;
		break;

	case REOP_DEC_JMP:
	case REOP_DEC_JMP_AND_RETURN_ONFAIL:
		p_instr += 3;
		break;

	default:
		break;
	}
	return (byte_t *)p_instr;
}

/* Try to allocate a new variable */
PRIVATE WUNUSED NONNULL((1, 2)) bool
NOTHROW_NCX(CC re_compiler_allocvar)(struct re_compiler *__restrict self,
                                     uint8_t *__restrict p_vid) {
	if unlikely(self->rec_code->rc_nvars >= 0x100)
		return false;
	*p_vid = (uint8_t)self->rec_code->rc_nvars++;
	return true;
}


/* Check if code pointed-at by `pc' is able to match EPSILON
 * Code at `pc' must be terminated by `REOP_MATCHED_PERFECT' */
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_code_matches_epsilon)(byte_t const *__restrict pc) {
	byte_t opcode;
dispatch:
	opcode = *pc++;
	switch (opcode) {

	case REOP_EXACT:
	case REOP_EXACT_ASCII_ICASE:
	case REOP_EXACT_UTF8_ICASE:
	case_REOP_ANY_MIN_to_MAX:
	case REOP_BYTE:
	case REOP_NBYTE:
	case REOP_BYTE2:
	case REOP_NBYTE2:
	case REOP_RANGE:
	case REOP_NRANGE:
	case REOP_CONTAINS_UTF8:
	case REOP_NCONTAINS_UTF8:
	case REOP_CS_UTF8:
	case REOP_CS_BYTE:
	case REOP_NCS_UTF8:
	case REOP_GROUP_MATCH: /* If it was an epsilon-match, `REOP_GROUP_MATCH_Jn' would have been used. */
		return false;

	case_REOP_GROUP_MATCH_JMIN_to_JMAX:
		/* Group match, where matched group can itself match epsilon
		 * As such, this opcode, too,  is able to match epsilon  (so
		 * keep looking for something that makes epsilon impossible) */
	case REOP_GROUP_START:
	case REOP_GROUP_END:
	case_REOP_GROUP_END_JMIN_to_JMAX:
		++pc; /* gid */
		goto dispatch;

	case_REOP_AT_MIN_to_MAX:
	case REOP_POP_ONFAIL:
	case REOP_JMP_ONFAIL_DUMMY:
	case REOP_NOP:
		goto dispatch;

	case REOP_POP_ONFAIL_AT:
	case REOP_JMP_ONFAIL_DUMMY_AT:
	case REOP_SETVAR:
	case REOP_MAYBE_POP_ONFAIL:
		pc += 2;
		goto dispatch;

	case REOP_JMP_ONFAIL:
	case REOP_JMP_AND_RETURN_ONFAIL: {
		int16_t delta;
		delta = delta16_get(pc);
		pc += 2;
		if (delta > 0 && re_code_matches_epsilon(pc + delta))
			return true;
		goto dispatch;
	}

	case REOP_JMP: {
		int16_t delta;
		delta = delta16_get(pc);
		pc += 2;
		pc += delta;
		goto dispatch;
	}

	case REOP_DEC_JMP: {
		int16_t delta;
		++pc; /* varid */
		delta = delta16_get(pc);
		pc += 2;
		if (delta >= 0)
			pc += delta;
		goto dispatch;
	}

	case REOP_DEC_JMP_AND_RETURN_ONFAIL: {
		int16_t delta;
		++pc; /* varid */
		delta = delta16_get(pc);
		pc += 2;
		if (delta > 0 && re_code_matches_epsilon(pc + delta))
			return true;
		goto dispatch;
	}

	case REOP_MATCHED:
	case REOP_MATCHED_PERFECT:
		/* End reached before any proper match -> epsilon can match */
		return true;

	default: __builtin_unreachable();
	}
	__builtin_unreachable();
}



/* Check if `tolower(ch) != ch || toupper(ch) != ch' */
#define ascii_iscaseable(ch) isalpha(ch)


/* Encode a literal byte.
 * @param: literal_byte: The literal byte */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_literal_byte)(struct re_compiler *__restrict self,
                                                 byte_t literal_byte) {
	if (IF_ICASE(self->rec_parser.rep_syntax)) {
		char lower = (char)tolower((unsigned char)literal_byte);
		char upper = (char)toupper((unsigned char)literal_byte);
		if (lower != upper) {
			assertf((unsigned char)lower == (unsigned char)literal_byte ||
			        (unsigned char)upper == (unsigned char)literal_byte,
			        "This should always be the case under the C locale, "
			        "and KOS doesn't support any other locale");
			if (!re_compiler_putc(self, REOP_BYTE2))
				goto err_nomem;
			if ((unsigned char)lower > (unsigned char)upper) {
				/* `REOP_BYTE2' requires that the 2 bytes be sorted. */
				tswap(char, lower, upper);
			}
			if (!re_compiler_putc(self, (unsigned char)lower))
				goto err_nomem;
			if (!re_compiler_putc(self, (unsigned char)upper))
				goto err_nomem;
			goto done_literal;
		}
	}
	if (!re_compiler_putc(self, REOP_BYTE))
		goto err_nomem;
	if (!re_compiler_putc(self, literal_byte))
		goto err_nomem;
done_literal:
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}

#ifndef __LIBCCALL
#define __LIBCCALL /* nothing */
#endif /* !__LIBCCALL */

PRIVATE WUNUSED NONNULL((1, 2)) int
NOTHROW_NCX(__LIBCCALL compare_char32_t)(void const *a, void const *b) {
	char32_t lhs = *(char32_t const *)a;
	char32_t rhs = *(char32_t const *)b;
	if (lhs < rhs)
		return -1;
	if (lhs > rhs)
		return 1;
	return 0;
}

/* Encode a literal unicode character.
 * @param: literal_char: The literal unicode character */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_literal_uni)(struct re_compiler *__restrict self,
                                                char32_t literal_char) {
	char utf8[UNICODE_UTF8_CURLEN];
	size_t utf8_len;
	utf8_len = (size_t)(unicode_writeutf8(utf8, literal_char) - utf8);
	if unlikely(utf8_len == 1) {
		byte_t literal_byte = (byte_t)(unsigned char)utf8[0];
		return re_compiler_compile_literal_byte(self, literal_byte);
	}

	/* Match unicode character */
	if (IF_ICASE(self->rec_parser.rep_syntax)) {
		uint8_t nchars;
		/* XXX: Technically, `chars' should be the set of C,  such
		 *      that `unicode_tolower(tok) == unicode_tolower(C)',
		 *      but the unicode  database has no  way to  (easily)
		 *      determine this... */
		char32_t chars[4];
		char32_t lower = unicode_tolower(literal_char);
		char32_t upper = unicode_toupper(literal_char);
		char32_t title = unicode_totitle(literal_char);

		/* Collect different chars */
		nchars   = 1;
		chars[0] = literal_char;
		if (lower != literal_char)
			chars[nchars++] = lower;
		if (upper != lower && upper != literal_char)
			chars[nchars++] = upper;
		if (title != lower && title != upper && title != literal_char)
			chars[nchars++] = title;
		assert(nchars >= 1 && nchars <= 4);
		if (nchars > 1) {
			char icase_utf8[lengthof(chars) * UNICODE_UTF8_CURLEN], *endp;
			size_t icase_utf8_len;
			uint8_t i;
			/* Must sort `chars', as required by `REOP_CONTAINS_UTF8' */
			qsort(chars, nchars, sizeof(char32_t), &compare_char32_t);
			for (endp = icase_utf8, i = 0; i < nchars; ++i)
				endp = unicode_writeutf8(endp, chars[i]);
			icase_utf8_len = (size_t)(endp - icase_utf8);
			if (!re_compiler_putc(self, REOP_CONTAINS_UTF8))
				goto err_nomem;
			if (!re_compiler_putc(self, nchars))
				goto err_nomem;
			if unlikely(!re_compiler_putn(self, icase_utf8, icase_utf8_len))
				goto err_nomem;
			goto done_literal;
		}
	}

	/* Match a single unicode character (consisting of more than 1 byte) */
	assert(utf8_len >= 2);
	if (!re_compiler_putc(self, REOP_EXACT))
		goto err_nomem;
	if (!re_compiler_putc(self, (uint8_t)utf8_len))
		goto err_nomem;
	if unlikely(!re_compiler_putn(self, utf8, utf8_len))
		goto err_nomem;
done_literal:
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}


/* Encode an unescaped literal byte sequence.
 * @param: literal_byte_seq_start:  Literal sequence base pointer.
 * @param: literal_byte_seq_end:    Literal sequence end pointer. */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_unescaped_literal_byte_seq)(struct re_compiler *__restrict self,
                                                               char const *literal_byte_seq_start,
                                                               char const *literal_byte_seq_end) {
	size_t literal_seq_size;
	uint8_t reop_exact_opcode;

	/* Select the appropriate `REOP_EXACT' opcode */
	reop_exact_opcode = REOP_EXACT;
	if (IF_ICASE(self->rec_parser.rep_syntax)) {
		/* Can just use the regular code-path, only have the runtime
		 * use `memcasecmp(3)' instead of the usual `memcmp(3)' when
		 * comparing input. */
		reop_exact_opcode = REOP_EXACT_ASCII_ICASE;
	}

	/* Simple require an exact match with `literal_seq_start...literal_seq_end'.
	 * -> For this purpose, we can encode a sequence of `REOP_EXACT' instructions. */
	literal_seq_size = (size_t)(literal_byte_seq_end - literal_byte_seq_start);
	while (literal_seq_size > UINT8_MAX) {
		if (!re_compiler_putc(self, reop_exact_opcode))
			goto err_nomem;
		if (!re_compiler_putc(self, UINT8_MAX))
			goto err_nomem;
		if (!re_compiler_putn(self, literal_byte_seq_start, UINT8_MAX))
			goto err_nomem;
		literal_byte_seq_start += UINT8_MAX;
		literal_seq_size -= UINT8_MAX;
	}
	if (literal_seq_size >= 2) {
		if (!re_compiler_putc(self, reop_exact_opcode))
			goto err_nomem;
		if (!re_compiler_putc(self, (uint8_t)literal_seq_size))
			goto err_nomem;
		if (!re_compiler_putn(self, literal_byte_seq_start, literal_seq_size))
			goto err_nomem;
	} else if (literal_seq_size == 1) {
		return re_compiler_compile_literal_byte(self, literal_byte_seq_start[0]);
	}
/*done_literal:*/
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}

/* Threshold: when  at least this many ascii-only characters appear
 * next to each other in a literal sequence that also contains non-
 * ascii characters, then the ascii portion  is cut out of the  seq
 * and encoded on its own (via `REOP_EXACT_ASCII_ICASE')
 *
 * This is done because `REOP_EXACT_ASCII_ICASE' can be decoded
 * much faster than `REOP_EXACT_UTF8_ICASE' */
#define UTF8_ICASE_ASCII_CHUNK_THRESHOLD 16


/* Encode an unescaped literal sequence.
 * @param: literal_seq_start:  Literal sequence base pointer.
 * @param: literal_seq_end:    Literal sequence end pointer.
 * @param: literal_seq_length: Literal sequence length (in characters; NOT bytes; assumed to be >= 2)
 * @param: literal_seq_isutf8: True if literal sequence is utf-8 encoded (else: it's pure bytes) */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_unescaped_literal_seq)(struct re_compiler *__restrict self,
                                                          char const *literal_seq_start,
                                                          char const *literal_seq_end,
                                                          size_t literal_seq_length,
                                                          bool literal_seq_isutf8) {
	if (!literal_seq_isutf8 || !(IF_ICASE(self->rec_parser.rep_syntax))) {
		/* If  input is ascii-only, or if casing isn't being ignored, then
		 * we can simply compile the entire literal sequence byte-by-byte. */
		return re_compiler_compile_unescaped_literal_byte_seq(self, literal_seq_start, literal_seq_end);
	}

	assert(IF_ICASE(self->rec_parser.rep_syntax));
	assert(literal_seq_isutf8);
	/* Must encode a unicode sequence: REOP_EXACT_UTF8_ICASE */
	while (literal_seq_length > 0) {
		/* Check if we can encode a leading ascii-sequence without using `REOP_EXACT_UTF8_ICASE' */
		size_t num_bytes = (size_t)(literal_seq_end - literal_seq_start);
		size_t num_leading_chars = 0;
		char const *literal_seq_iter;
		while (num_leading_chars < num_bytes &&
		       (unsigned char)literal_seq_start[num_leading_chars] < 0x80)
			++num_leading_chars;
		assert(literal_seq_length >= num_leading_chars);
		if (num_leading_chars >= UTF8_ICASE_ASCII_CHUNK_THRESHOLD) {
			/* Encode the ascii-only portion separately */
			re_errno_t error;
			error = re_compiler_compile_unescaped_literal_byte_seq(self,
			                                                       literal_seq_start,
			                                                       literal_seq_start + num_leading_chars);
			if unlikely(error != RE_NOERROR)
				return error;
			literal_seq_start += num_leading_chars;
			literal_seq_length -= num_leading_chars;
			if (!literal_seq_length)
				break;
			assert(literal_seq_start < literal_seq_end);
			num_leading_chars = 0;
		}

		/* Figure out how many unicode characters there are.
		 *
		 * If we reach a gap of at least `UTF8_ICASE_ASCII_CHUNK_THRESHOLD'
		 * ascii-only characters, then  only emit code  up until said  gap!
		 * Also: only parse a sequence of up to UINT8_MAX unicode characters,
		 *       since the `REOP_EXACT_UTF8_ICASE'  opcode can't handle  more
		 *       than that many! */
		literal_seq_iter = literal_seq_start;
		while (num_leading_chars < literal_seq_length &&
		       num_leading_chars < UINT8_MAX) {
			assert(literal_seq_iter < literal_seq_end);
			if ((unsigned char)*literal_seq_iter < 0x80) {
				char const *literal_ascii_end;
				size_t literal_ascii_size = 1;
				literal_ascii_end = literal_seq_iter;
				++literal_ascii_end;
				while ((literal_ascii_end < literal_seq_end) &&
				       ((unsigned char)*literal_ascii_end < 0x80) &&
				       (literal_ascii_size < UTF8_ICASE_ASCII_CHUNK_THRESHOLD)) {
					++literal_ascii_end;
					++literal_ascii_size;
				}
				if (literal_ascii_size >= UTF8_ICASE_ASCII_CHUNK_THRESHOLD) {
					/* Emit the current utf-8 chunk, and have the ascii detector
					 * above deal with  the ascii-chunk once  the current  utf-8
					 * chunk has been written. */
					break;
				}
				/* Ascii-chunk  is too small for non-utf-8 encoding to be more efficient.
				 * As such, advance to its end and include it in the current utf-8 chunk. */
				assert(literal_seq_iter + literal_ascii_size == literal_ascii_end);
				literal_seq_iter = literal_ascii_end;
				num_leading_chars += literal_ascii_size;
				assert(num_leading_chars <= literal_seq_length);
				if (num_leading_chars > UINT8_MAX) {
					size_t too_many = num_leading_chars - UINT8_MAX;
					num_leading_chars -= too_many;
					literal_seq_iter -= too_many;
				}
				continue;
			}
			literal_seq_iter += unicode_utf8seqlen[(unsigned char)*literal_seq_iter];
			++num_leading_chars;
		}

		/* Encode  the  `num_leading_chars'-character long
		 * chunk at `literal_seq_start...literal_seq_iter' */
		assert(num_leading_chars > 0);
		if (!re_compiler_putc(self, REOP_EXACT_UTF8_ICASE))
			goto err_nomem;
		if (!re_compiler_putc(self, (uint8_t)num_leading_chars))
			goto err_nomem;
		if (!re_compiler_putn(self, literal_seq_start, (size_t)(literal_seq_iter - literal_seq_start)))
			goto err_nomem;

		/* Continue with the remainder */
		literal_seq_start = literal_seq_iter;
		literal_seq_length -= num_leading_chars;
	}

/*done_literal:*/
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}

/* Encode a literal sequence.
 * @param: literal_seq_start:  Literal sequence base pointer.
 * @param: literal_seq_end:    Literal sequence end pointer.
 * @param: literal_seq_length: Literal sequence length (in characters; NOT bytes; assumed to be >= 1)
 * @param: literal_seq_hasesc: True if literal sequence still contains regex escapes
 * @param: literal_seq_isutf8: True if literal sequence is utf-8 encoded (else: it's pure bytes) */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_literal_seq)(struct re_compiler *__restrict self,
                                                char const *literal_seq_start,
                                                char const *literal_seq_end,
                                                size_t literal_seq_length,
                                                bool literal_seq_hasesc,
                                                bool literal_seq_isutf8) {
	char *unesc_literal_seq_start;
	char *unesc_literal_seq_end;
	re_errno_t error;

	/* At this point, we want to encode a literal sequence:
	 *     [literal_seq_start, literal_seq_end)
	 * Which is still  encoded as a  `literal_seq_length-character',
	 * regex encoded sequence of literal characters, with properties
	 * described by `literal_seq_hasesc' and `literal_seq_isutf8'. */
	assert(literal_seq_length >= 1);
	assert(self->rec_parser.rep_pos == literal_seq_end);
	if (literal_seq_length == 1) {
		/* Special case: single-character literal */
		re_token_t literal_tok;
		self->rec_parser.rep_pos = literal_seq_start;
		literal_tok = re_compiler_yield(self);
		assert(self->rec_parser.rep_pos == literal_seq_end);
		assert(RE_TOKEN_ISLITERAL(literal_tok));
		if (literal_seq_isutf8) {
			assert(RE_TOKEN_ISUTF8(literal_tok));
			return re_compiler_compile_literal_uni(self, (char32_t)literal_tok);
		}
		assert(!RE_TOKEN_ISUTF8(literal_tok));
		if (RE_TOKEN_ISBYTE80h(literal_tok))
			literal_tok = RE_TOKEN_GETBYTE80h(literal_tok);
		return re_compiler_compile_literal_byte(self, (byte_t)literal_tok);
	}

	/* multi-character literal (using `REOP_EXACT' and friends) */
	assert(literal_seq_length >= 2);
	unesc_literal_seq_start = (char *)literal_seq_start;
	unesc_literal_seq_end   = (char *)literal_seq_end;
	if (literal_seq_hasesc) {
		/* Must unescape the literal sequence and convert it to utf-8/bytes */
		char *unesc_literal_seq_iter;
		size_t unesc_seq_maxlen = literal_seq_length;
#ifndef NDEBUG
		size_t unesc_numchars;
#endif /* !NDEBUG */
		if (literal_seq_isutf8) {
			/* No \-sequence can ever be shorter than the character it represents */
			unesc_seq_maxlen = (size_t)(literal_seq_end -
			                            literal_seq_start);
		}
		unesc_literal_seq_start = (char *)malloc(unesc_seq_maxlen);
		if unlikely(!unesc_literal_seq_start)
			goto err_nomem;
		unesc_literal_seq_iter = unesc_literal_seq_start;
		self->rec_parser.rep_pos = literal_seq_start;
#ifndef NDEBUG
		unesc_numchars = 0;
#endif /* !NDEBUG */
		while (self->rec_parser.rep_pos < literal_seq_end) {
			re_token_t lit = re_compiler_yield(self);
			assert(RE_TOKEN_ISLITERAL(lit));
			if (RE_TOKEN_ISBYTE80h(lit)) {
				*unesc_literal_seq_iter++ = RE_TOKEN_GETBYTE80h(lit);
			} else if (lit >= 0x80 && literal_seq_isutf8) {
				unesc_literal_seq_iter = unicode_writeutf8(unesc_literal_seq_iter, lit);
			} else {
				*unesc_literal_seq_iter++ = (byte_t)lit;
			}
#ifndef NDEBUG
			++unesc_numchars;
#endif /* !NDEBUG */
		}
#ifndef NDEBUG
		assertf(unesc_numchars == literal_seq_length,
		        "First time around we got nchars:  %Iu\n"
		        "Second time around we got nchars: %Iu",
		        literal_seq_length, unesc_numchars);
#endif /* !NDEBUG */
		unesc_literal_seq_end = unesc_literal_seq_iter;
		assert(unesc_literal_seq_end <= unesc_literal_seq_start + unesc_seq_maxlen);
	}

	/* Compile the unescaped literal sequence */
	error = re_compiler_compile_unescaped_literal_seq(self,
	                                                  unesc_literal_seq_start,
	                                                  unesc_literal_seq_end,
	                                                  literal_seq_length,
	                                                  literal_seq_isutf8);
	if (literal_seq_hasesc)
		free(unesc_literal_seq_start);
	return error;
err_nomem:
	return RE_ESPACE;
}


#if ALTERNATION_PREFIX_MAXLEN > 0
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_alternation)(struct re_compiler *__restrict self,
                                                void const *alternation_prefix,
                                                size_t alternation_prefix_size);
#else /* ALTERNATION_PREFIX_MAXLEN > 0 */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC _re_compiler_compile_alternation)(struct re_compiler *__restrict self);
#define re_compiler_compile_alternation(self, ...) _re_compiler_compile_alternation(self)
#endif /* ALTERNATION_PREFIX_MAXLEN <= 0 */

struct charclass_def {
	char    csd_name[7]; /* Charset name */
	uint8_t csd_opcode;  /* Charset opcode (one of `RECS_*') */
};


/* Known character classes, and their corresponding `RECS_*' opcodes. */
PRIVATE struct charclass_def const charclass_opcodes[] = {
	{ "cntrl", RECS_ISCNTRL },
	{ "space", RECS_ISSPACE },
	{ "upper", RECS_ISUPPER },
	{ "lower", RECS_ISLOWER },
	{ "alpha", RECS_ISALPHA },
	{ "digit", RECS_ISDIGIT },
	{ "xdigit", RECS_ISXDIGIT },
	{ "alnum", RECS_ISALNUM },
	{ "punct", RECS_ISPUNCT },
	{ "graph", RECS_ISGRAPH },
	{ "print", RECS_ISPRINT },
	{ "blank", RECS_ISBLANK },
	{ { 's', 'y', 'm', 's', 't', 'r', 't' }, RECS_ISSYMSTRT },
	{ { 's', 'y', 'm', 'c', 'o', 'n', 't' }, RECS_ISSYMCONT },
	{ "tab", RECS_ISTAB },
	{ "white", RECS_ISWHITE },
	{ "empty", RECS_ISEMPTY },
	{ "lf", RECS_ISLF },
	{ "hex", RECS_ISHEX },
	{ "title", RECS_ISTITLE },
	{ { 'n', 'u', 'm', 'e', 'r', 'i', 'c' }, RECS_ISNUMERIC },
};

/* Find  the charset definition, given its `name'
 * Returns `RECS_DONE' if no such charset exists. */
PRIVATE WUNUSED uint8_t
NOTHROW_NCX(CC charset_find)(char const *__restrict name, size_t namelen) {
	size_t i;
	if (namelen > lengthof(charclass_opcodes[0].csd_name))
		return RECS_DONE;
	for (i = 0; i < lengthof(charclass_opcodes); ++i) {
		struct charclass_def const *set = &charclass_opcodes[i];
		if (bcmp(set->csd_name, name, namelen) == 0 &&
		    (namelen >= lengthof(charclass_opcodes[i].csd_name) ||
		     set->csd_name[namelen] == '\0'))
			return set->csd_opcode;
	}
	return RECS_DONE;
}

struct unicode_charset {
	char  *ucs_basep; /* [0..1][<= ucs_endp][owned] Charset base pointer.
	                   *
	                   * The charset itself is a tightly packed utf-8  string,
	                   * with all of its containing characters sorted by their
	                   * unicode ordinal values.
	                   *
	                   * Because  unicode lead bytes can be differentiated from
	                   * unicode follow-up bytes, it is always possible to find
	                   * the start of a  character, given an arbitrary  pointer
	                   * into its sequence (thus: this array can be  bsearch'd) */
	char  *ucs_endp;  /* [0..1][>= ucs_basep] Charset end pointer. */
	size_t ucs_count; /* # of unicode characters in this set. */
#ifdef LIBREGEX_NO_MALLOC_USABLE_SIZE
	size_t ucs_basep_avail; /* Allocated size of `ucs_basep' */
#define unicode_charset_get_basep_avail(self)    (self)->ucs_basep_avail
#define unicode_charset_set_basep_avail(self, v) (void)((self)->ucs_basep_avail = (v))
#else /* LIBREGEX_NO_MALLOC_USABLE_SIZE */
#define unicode_charset_get_basep_avail(self)    malloc_usable_size((self)->ucs_basep)
#define unicode_charset_set_basep_avail(self, v) (void)0
#endif /* !LIBREGEX_NO_MALLOC_USABLE_SIZE */
};

#define UNICODE_CHARSET_INIT_IS_ZERO
#define unicode_charset_init(self)                      \
	(void)((self)->ucs_basep = (self)->ucs_endp = NULL, \
	       (self)->ucs_count                    = 0,    \
	       unicode_charset_set_basep_avail(self, 0))
#define unicode_charset_fini(self)    free((self)->ucs_basep)
#define unicode_charset_isempty(self) ((self)->ucs_basep <= (self)->ucs_endp)

/* Insert (if it's not already contained) the given `ch' into `self'
 * @return:  1: The given `ch' was already contained in `self' (and thus wasn't inserted)
 * @return:  0: The given `ch' has been inserted into `self'
 * @return: -1: Error: insufficient memory. */
PRIVATE WUNUSED NONNULL((1)) int
NOTHROW_NCX(CC unicode_charset_insert)(struct unicode_charset *__restrict self,
                                       char32_t ch) {
	size_t utf8_len, oldsize, newsize, avlsize;
	char utf8[UNICODE_UTF8_CURLEN];
	char *lo, *hi;
	lo = self->ucs_basep;
	hi = self->ucs_endp;
	while (lo < hi) {
		char32_t mch;
		char *mid;
		mid = lo + ((size_t)(hi - lo) >> 1);
		mid = utf8_baseptr(mid);
		mch = utf8_charat(mid);
		if (ch < mch) {
			hi = mid;
		} else if (ch > mch) {
			lo = mid + unicode_utf8seqlen[(unsigned char)*mid];
		} else {
			/* Already contained in set. */
			return 1;
		}
	}
	assert(lo == hi);

	/* Must insert `ch' at `lo' */
	utf8_len = (size_t)(unicode_writeutf8(utf8, ch) - utf8);
	oldsize  = (size_t)(self->ucs_endp - self->ucs_basep);
	newsize  = oldsize + utf8_len;
	avlsize  = unicode_charset_get_basep_avail(self);
	if (newsize > avlsize) {
		char *newbuf;
		size_t newalloc = avlsize * 2;
		if (newalloc < 16)
			newalloc = 16;
		if (newalloc < newsize)
			newalloc = newsize;
		newbuf = (char *)reallocv(self->ucs_basep, newalloc, sizeof(char));
		if unlikely(!newbuf) {
			newalloc = newsize;
			newbuf   = (char *)reallocv(self->ucs_basep, newalloc, sizeof(char));
			if unlikely(!newbuf)
				return -1;
		}
		lo = newbuf + (lo - self->ucs_basep);
		hi = newbuf + (hi - self->ucs_basep);
		self->ucs_basep = newbuf;
		self->ucs_endp  = newbuf + oldsize;
		unicode_charset_set_basep_avail(self, newalloc);
	}

	/* Insert the utf8-sequence at the required offset. */
	memmoveupc(lo + utf8_len, lo, (size_t)(self->ucs_endp - lo), sizeof(char));
	memcpyc(lo, utf8, utf8_len, sizeof(char));
	self->ucs_endp += utf8_len;
	assert(self->ucs_endp == self->ucs_basep + newsize);
	++self->ucs_count;
	return 0;
}

PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC charvec_contains)(char32_t const *chv, size_t chc, char32_t ch) {
	size_t i;
	for (i = 0; i < chc; ++i) {
		if (chv[i] == ch)
			return true;
	}
	return false;
}

PRIVATE WUNUSED NONNULL((1, 2)) int
NOTHROW_NCX(CC unicode_charset_insertall)(struct unicode_charset *__restrict self,
                                          char32_t const *chv, size_t chc) {
	size_t i;
	if (chc > 0) {
		if unlikely(unicode_charset_insert(self, chv[0]) == -1)
			goto err;
		if (chc > 1) {
			for (i = 1; i < chc; ++i) {
				char32_t ch = chv[i];
				if (charvec_contains(chv, i, ch))
					continue;
				if unlikely(unicode_charset_insert(self, ch) == -1)
					goto err;
			}
		}
	}
	return 0;
err:
	return -1;
}

/* Generate code `RECS_RANGE lo, hi' */
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_compiler_gen_RECS_RANGE)(struct re_compiler *__restrict self,
                                           char32_t lo, char32_t hi) {
	size_t codelen;
	byte_t code[1 + (2 * UNICODE_UTF8_CURLEN)], *writer;
	writer    = code;
	*writer++ = IF_ICASE(self->rec_parser.rep_syntax) ? (uint8_t)RECS_RANGE_ICASE
	                                                  : (uint8_t)RECS_RANGE;
	writer    = (byte_t *)unicode_writeutf8((char *)writer, lo);
	writer    = (byte_t *)unicode_writeutf8((char *)writer, hi);
	codelen   = (size_t)(writer - code);
	return re_compiler_putn(self, code, codelen);
}



struct re_charset {
#define CHARCLASS_COUNT ((RECS_ISX_MAX - RECS_ISX_MIN) + 1)
	bitset_t   bitset_decl(rc_bytes, 256);                   /* Raw bytes (and ASCII characters) */
	bitset_t   bitset_decl(rc_charclasses, CHARCLASS_COUNT); /* Unicode character classes */
	struct unicode_charset rc_uchars;                        /* Extra unicode characters to add to the set. */
	bool                   rc_negate;                        /* True if set is being negated */
};

#ifdef UNICODE_CHARSET_INIT_IS_ZERO
#define re_charset_init(self) \
	bzero(self, sizeof(struct re_charset))
#else /* UNICODE_CHARSET_INIT_IS_ZERO */
#define re_charset_init(self)                \
	(bzero(self, sizeof(struct re_charset)), \
	 unicode_charset_init(&(self)->rc_uchars))
#endif /* !UNICODE_CHARSET_INIT_IS_ZERO */
#define re_charset_fini(self) \
	unicode_charset_fini(&(self)->rc_uchars)




#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4701) /* Bogus warning */
#endif /* _MSC_VER */

/* Yield a charset literal. Returns:
 * - A unicode ordinal  (return < RE_TOKEN_BASE)
 * - A raw byte literal (return >= RE_TOKEN_BYTE80h_MIN && return <= RE_TOKEN_BYTE80h_MAX)
 * - RE_TOKEN_EOF when the trailing '\0'-char is reached
 * - RE_TOKEN_UNMATCHED_BK when an unmatched '\' is encountered
 * - RE_TOKEN_ILLSEQ when at an illegal unicode sequence
 */
PRIVATE WUNUSED NONNULL((1)) char32_t
NOTHROW_NCX(CC re_parser_yield_cs_literal)(struct re_parser *__restrict self) {
	unsigned char ch;
	ch = (unsigned char)*self->rep_pos++;
	if (ch >= 0x80) {
		char32_t uni;
		uint8_t i, seqlen;
decpos_and_readutf8:
		--self->rep_pos;
		/* Make sure that `unicode_readutf8(3)' won't access out-of-bounds memory */
		seqlen = unicode_utf8seqlen[ch];
		for (i = 0; i < seqlen; ++i) {
			if (self->rep_pos[i] == '\0' && (self->rep_pos + i) >= self->rep_end)
				return RE_TOKEN_EOF;
		}
		uni = unicode_readutf8(&self->rep_pos);
		if unlikely(uni >= RE_TOKEN_BASE) {
			self->rep_pos -= seqlen;
			return RE_TOKEN_ILLSEQ;
		}
		return uni;
	}
	assert(self->rep_pos - 1 <= self->rep_end);
	if (ch == '\0' && (self->rep_pos - 1 >= self->rep_end)) {
		--self->rep_pos;
		return RE_TOKEN_EOF;
	}
	if ((ch == '\\') &&
	    IF_BACKSLASH_ESCAPE_IN_LISTS(self->rep_syntax) && !IF_NO_KOS_OPS(self->rep_syntax)) {
		ch = (unsigned char)*self->rep_pos++;
		switch (ch) {

		case '\0':
			assert(self->rep_pos - 1 <= self->rep_end);
			if (self->rep_pos - 1 >= self->rep_end) {
				self->rep_pos -= 2; /* Keep on repeating this token! */
				return RE_TOKEN_UNMATCHED_BK;
			}
			goto default_escaped_char;

		case '0': {
			uint32_t ord;
			unsigned int i, ndigits;
			if (IF_NO_KOS_OPS(self->rep_syntax))
				break;
			ord     = 0;
			ndigits = 3;
			for (i = 0; i < ndigits; ++i, ++self->rep_pos) {
				uint8_t nibble;
				char octchar = *self->rep_pos;
				if (octchar >= '0' && octchar <= '7') {
					nibble = (uint8_t)(octchar - '0');
				} else {
					break;
				}
				ord <<= 3;
				ord |= nibble;
			}
			if (ord >= 0x80) {
				ord -= 0x80;
				ord += RE_TOKEN_BYTE80h_MIN;
			}
			return ord;
		}	break;

		case 'u': /* TODO: Support for unicode's "\u{ABC DEF 123}" -> "\u0ABC\u0DEF\u0123" */
		case 'U':
			if (self->rep_syntax & RE_SYNTAX_NO_UTF8)
				break;
			ATTR_FALLTHROUGH
		case 'x': {
			uint32_t ord;
			unsigned int i, ndigits;
			if (IF_NO_KOS_OPS(self->rep_syntax))
				break;
			ndigits = ch == 'U' ? 8 : ch == 'u' ? 4 : 2;
			ord     = 0;
			for (i = 0; i < ndigits; ++i, ++self->rep_pos) {
				uint8_t nibble;
				char hexchar = *self->rep_pos;
				if (!__libc_hex2int(hexchar, &nibble)) {
					self->rep_pos -= i;
					goto default_escaped_char;
				}
				ord <<= 4;
				ord |= nibble;
			}
			if unlikely(ord >= RE_TOKEN_BASE) {
				/* Guard against illegal unicode characters. */
				self->rep_pos -= ndigits;
				goto default_escaped_char;
			}
			if (ch == 'x') {
				/* Special case: This one's 80h-FFh must be encoded as `RE_TOKEN_BYTE80h_MIN',
				 *               as it's not supposed to  match U+0080-U+00FF, but rather  the
				 *               raw bytes 80h-FFh. */
				if (ord >= 0x80) {
					ord -= 0x80;
					ord += RE_TOKEN_BYTE80h_MIN;
				}
			}
			return ord;
		}	break;

		default:
			if (ch >= 0x80 /*&& ch <= 0xff*/)
				goto decpos_and_readutf8;

default_escaped_char:
			break;
		}
	}
	return ch;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif /* _MSC_VER */

/* Parse a collating character, or a single unicode character
 * - Returns `RE_TOKEN_EOF' if EOF was reached
 * - Returns `RE_TOKEN_ILLSEQ' when at an illegal unicode sequence */
PRIVATE WUNUSED NONNULL((1)) re_token_t
NOTHROW_NCX(CC re_parser_yield_collating_char)(struct re_parser *__restrict self) {
	unsigned char ch;
	size_t len;
	ch = *self->rep_pos;
	if (ch >= 0x80) /* Unicode chars can never be apart of collations */
		return re_parser_yield_cs_literal(self);
	if unlikely(!ch && self->rep_pos >= self->rep_end)
		return RE_TOKEN_EOF;
	len = 1;
	while (self->rep_pos[len] != '.' &&
	       self->rep_pos[len] != '=' &&
	       self->rep_pos[len] != '\0')
		++len;
	if (len > 1) {
		/* Check if we can find the named collating char in `posix_locale_cchars' */
		size_t lo = 0, hi = lengthof(posix_locale_cchars);
		while (lo < hi) {
			int diff;
			size_t i;
			i    = lo + ((hi - lo) >> 1);
			diff = strcmpz(posix_locale_cchars[i].cc_name,
			               self->rep_pos, len);
			if (diff < 0) {
				lo = i + 1;
			} else if (diff > 0) {
				hi = i;
			} else {
				/* Found it! */
				self->rep_pos += len;
				return (re_token_t)(unsigned char)posix_locale_cchars[i].cc_char;
			}
		}
	}

	/* Regular, old ASCII character */
	++self->rep_pos;
	return ch;
}

PRIVATE NONNULL((1, 2)) re_errno_t
NOTHROW_NCX(CC re_charset_adduchar)(struct re_charset *__restrict self,
                                    struct re_compiler const *__restrict compiler,
                                    char32_t ch) {
	if (ch < 0x80) {
		/* Simple case: ascii-only char */
		bitset_set(self->rc_bytes, (unsigned char)ch);
	} else if (compiler->rec_parser.rep_syntax & RE_SYNTAX_ICASE) {
		char32_t chars[4];
		chars[0] = ch;
		chars[1] = unicode_tolower(ch);
		chars[2] = unicode_toupper(ch);
		chars[3] = unicode_totitle(ch);
		if unlikely(unicode_charset_insertall(&self->rc_uchars, chars, 4) < 0)
			goto err_nomem;
	} else {
		if unlikely(unicode_charset_insert(&self->rc_uchars, ch) < 0)
			goto err_nomem;
	}
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4701) /* Bogus warning */
#endif /* _MSC_VER */

PRIVATE WUNUSED NONNULL((1, 2)) re_errno_t
NOTHROW_NCX(CC re_compiler_parse_charset)(struct re_compiler *__restrict self,
                                          struct re_charset *__restrict result) {
	if (*self->rec_parser.rep_pos == '^') {
		result->rc_negate = true;
		++self->rec_parser.rep_pos;
	}

	/* Special case: if the character immediately after the open '[' is
	 * either ']' or '-', it will  not have its usual special  meaning. */
	if (*self->rec_parser.rep_pos == ']' || *self->rec_parser.rep_pos == '-') {
		bitset_set(result->rc_bytes, *self->rec_parser.rep_pos);
		++self->rec_parser.rep_pos;
	}

loop_next:
	for (;;) {
		char32_t lochar, hichar;
		unsigned char ch;
		ch = (unsigned char)*self->rec_parser.rep_pos++;
		switch (ch) {

		case '\0':
			assert(self->rec_parser.rep_pos - 1 <= self->rec_parser.rep_end);
			if (self->rec_parser.rep_pos - 1 >= self->rec_parser.rep_end)
				return RE_EEND;  /* Actual EOF */
			goto encode_literal; /* Literal NUL-byte */

		case ']':
			goto done_loop;

		case '[':
			if (IF_CHAR_CLASSES(self->rec_parser.rep_syntax) &&
			    (*self->rec_parser.rep_pos == ':')) {
				/* character classes */
				uint8_t cs_opcode;
				char const *csend, *csstart;
				self->rec_parser.rep_pos += 1;
				csstart = self->rec_parser.rep_pos;
				csend   = strchr(csstart, ':');
				if unlikely(!csend)
					return RE_EEND;
				cs_opcode = charset_find(csstart, (size_t)(csend - csstart));
				if unlikely(cs_opcode == RECS_DONE)
					return RE_ECTYPE;
				if (IF_ICASE(self->rec_parser.rep_syntax)) {
					/* Transform char-classes in ICASE-mode */
					if (cs_opcode == RECS_ISUPPER || cs_opcode == RECS_ISLOWER || cs_opcode == RECS_ISTITLE)
						cs_opcode = RECS_ISALNUM;
				}
				csend += 1;
				if unlikely(*csend != ']')
					return RE_BADPAT;
				csend += 1;
				self->rec_parser.rep_pos = csend;

				/* Add the selected charset to the collection of ones that have been used. */
				bitset_set(result->rc_charclasses, cs_opcode - RECS_ISX_MIN);
				goto loop_next;
			}
			if ((*self->rec_parser.rep_pos == '.') ||
			    (*self->rec_parser.rep_pos == '=')) {
				/* Collating elements and equivalence classes */
				char collmode = *self->rec_parser.rep_pos;
				self->rec_parser.rep_pos += 1;
				lochar = re_parser_yield_collating_char(&self->rec_parser);
				if unlikely(RE_TOKEN_ISERROR(lochar))
					return RE_TOKEN_GETERROR(lochar);
				if unlikely(*self->rec_parser.rep_pos != collmode)
					return RE_ECOLLATE;
				++self->rec_parser.rep_pos;
				if unlikely(*self->rec_parser.rep_pos != ']')
					return RE_EBRACK;
				++self->rec_parser.rep_pos;
				if (collmode == '=') {
					/* TODO: Unicode equivalence classes (libc's <unicode.h> needs an API for this):
					 * >> "[[=a=]]" must be similar to like "[aäâ]"  (plus any other character) */
				}
				goto encode_uchar_or_byte80h;
			}
			goto encode_literal;

		case '\\':
			if (IF_BACKSLASH_ESCAPE_IN_LISTS(self->rec_parser.rep_syntax)) {
				/* Special case: backslash-escape sequences are allowed in character classes */
				ch = (unsigned char)*self->rec_parser.rep_pos++;
				assert(self->rec_parser.rep_pos - 1 <= self->rec_parser.rep_end);
				if unlikely(ch == '\0' && (self->rec_parser.rep_pos - 1 >= self->rec_parser.rep_end)) {
					--self->rec_parser.rep_pos;
					return RE_EESCAPE;
				}
				if (!IF_NO_KOS_OPS(self->rec_parser.rep_syntax)) {
					switch (ch) {

					case 'w':
						bitset_set(result->rc_charclasses, RECS_ISSYMCONT - RECS_ISX_MIN);
						goto loop_next;

					case 'n':
						bitset_set(result->rc_charclasses, RECS_ISLF - RECS_ISX_MIN);
						goto loop_next;

					case 's':
						bitset_set(result->rc_charclasses, RECS_ISSPACE - RECS_ISX_MIN);
						goto loop_next;

					case 'd':
						bitset_set(result->rc_charclasses, RECS_ISDIGIT - RECS_ISX_MIN);
						goto loop_next;

					case 'u':
						if (*self->rec_parser.rep_pos == '{' &&
						    !(IF_NO_UTF8(self->rec_parser.rep_syntax))) {
							/* Parse something like "\u{ABC DEF 123 456}"
							 * -> same as "\u0ABC\u0DEF\u0123\u0456" */
							++self->rec_parser.rep_pos;
							while (ASCII_ISSPACE(*self->rec_parser.rep_pos))
								++self->rec_parser.rep_pos;
again_unicode_brace_char:
							if (!__libc_hex2int(*self->rec_parser.rep_pos, &lochar)) {
								if (self->rec_parser.rep_pos >= self->rec_parser.rep_end)
									return RE_EEND;
								return RE_EILLSEQ;
							}
							++self->rec_parser.rep_pos;
							for (;;) {
								uint8_t nibble;
								if (!__libc_hex2int(*self->rec_parser.rep_pos, &nibble))
									break;
								if unlikely(OVERFLOW_UMUL(lochar, 16, &lochar))
									return RE_EILLSEQ;
								if unlikely(OVERFLOW_UADD(lochar, nibble, &lochar))
									return RE_EILLSEQ;
								++self->rec_parser.rep_pos;
							}
							if unlikely(lochar >= RE_TOKEN_BASE)
								return RE_EILLSEQ;
							if (*self->rec_parser.rep_pos != '}') {
								/* Skip trailing space after unicode ordinals */
								while (ASCII_ISSPACE(*self->rec_parser.rep_pos))
									++self->rec_parser.rep_pos;
								if (*self->rec_parser.rep_pos != '}') {
									re_errno_t addchar_error;
									if unlikely(self->rec_parser.rep_pos >= self->rec_parser.rep_end)
										return RE_EEND;
									addchar_error = re_charset_adduchar(result, self, lochar);
									if unlikely(addchar_error != RE_NOERROR)
										return addchar_error;
									goto again_unicode_brace_char;
								}
							}
							++self->rec_parser.rep_pos; /* Skip '}' */
							goto encode_uchar; /* Encode last uchar the normal way (including range support) */
						}
						ATTR_FALLTHROUGH
					case '0':
					case 'U':
					case 'x':
						self->rec_parser.rep_pos -= 2;
						lochar = re_parser_yield_cs_literal(&self->rec_parser);
						assert(lochar != RE_TOKEN_EOF);
						if unlikely(RE_TOKEN_ISERROR(lochar))
							return RE_TOKEN_GETERROR(lochar);
encode_uchar_or_byte80h:
						if (RE_TOKEN_ISBYTE80h(lochar)) {
							ch = (unsigned char)(byte_t)RE_TOKEN_GETBYTE80h(lochar);
							goto encode_byte_ch;
						}
						goto encode_uchar;

					default: break;
					}
				}
				goto encode_literal;
			}
			goto encode_literal;

		default:
encode_literal:
			if (ch >= 0x80 && !(IF_NO_UTF8(self->rec_parser.rep_syntax))) {
				--self->rec_parser.rep_pos;
				lochar = unicode_readutf8((char const **)&self->rec_parser.rep_pos); /* TODO: This doesn't handle EOF properly! */
encode_uchar:
				if unlikely(lochar < 0x80) {
					ch = (unsigned char)lochar;
					goto encode_byte_ch;
				}
				if (self->rec_parser.rep_pos[0] == '-' &&
				    self->rec_parser.rep_pos[1] != ']') {
					/* Unicode character range. */
					++self->rec_parser.rep_pos; /* Skip over '-' character */
					hichar = re_parser_yield_cs_literal(&self->rec_parser);
					if unlikely(RE_TOKEN_ISERROR(hichar))
						return RE_TOKEN_GETERROR(hichar);
					if (RE_TOKEN_ISBYTE80h(hichar))
						goto handle_bad_unicode_range; /* Something like "[ä-\xAB]" isn't allowed */
encode_unicode_range:
					if (hichar < lochar) {
handle_bad_unicode_range:
						/* Bad range lo/hi bounds */
						if (IF_NO_EMPTY_RANGES(self->rec_parser.rep_syntax))
							return RE_ERANGE;
						goto loop_next; /* Ignore range. */
					}
					if (IF_ICASE(self->rec_parser.rep_syntax)) {
						lochar  = unicode_tolower(lochar);
						hichar = unicode_tolower(hichar);
						if unlikely(lochar > hichar)
							tswap(char32_t, lochar, hichar);
					}

					/* Special case: "[ä-ä]" is encoded as "[ä]" */
					if (lochar == hichar)
						goto add_lochar_to_unicode_charset;

					/* Directly encode the utf-8 sequence length. */
					if unlikely(!re_compiler_gen_RECS_RANGE(self, lochar, hichar))
						goto err_nomem;
					goto loop_next;
				}

				/* Add unicode character to unicode set. */
add_lochar_to_unicode_charset:
				if (IF_ICASE(self->rec_parser.rep_syntax)) {
					char32_t chars[4];
					chars[0] = lochar;
					chars[1] = unicode_tolower(lochar);
					chars[2] = unicode_toupper(lochar);
					chars[3] = unicode_totitle(lochar);
					if unlikely(unicode_charset_insertall(&result->rc_uchars, chars, 4) < 0)
						goto err_nomem;
				} else {
					if unlikely(unicode_charset_insert(&result->rc_uchars, lochar) < 0)
						goto err_nomem;
				}
				goto loop_next;
			}
encode_byte_ch:
			if (self->rec_parser.rep_pos[0] == '-' &&
			    self->rec_parser.rep_pos[1] != ']') {
				/* Ascii character range. */
				unsigned char hibyte;
				++self->rec_parser.rep_pos;
				hichar = re_parser_yield_cs_literal(&self->rec_parser);
				if unlikely(RE_TOKEN_ISERROR(hichar))
					return RE_TOKEN_GETERROR(hichar);
				if (RE_TOKEN_ISBYTE80h(hichar)) {
					hibyte = RE_TOKEN_GETBYTE80h(hichar);
				} else if (RE_TOKEN_ISUTF8(hichar)) {
					if unlikely(ch >= 0x80)
						goto handle_bad_byte_range; /* Something like "[\xAB-ä]" isn't allowed */
					/* Handle something like "[a-ä]" */
					lochar = (char32_t)ch;
					goto encode_unicode_range;
				} else {
					hibyte = (unsigned char)hichar;
				}
				if (hibyte < ch) {
handle_bad_byte_range:
					/* Bad range lo/hi bounds */
					if (IF_NO_EMPTY_RANGES(self->rec_parser.rep_syntax))
						return RE_ERANGE;
					goto loop_next; /* Ignore range. */
				}
				bitset_nset_r(result->rc_bytes, ch, hibyte);
			} else {
				bitset_set(result->rc_bytes, ch);
			}
			break;
		}
	}
done_loop:

	/* In ASCII-mode, we're not allowed to encode char classes. Instead,
	 * we have to essentially hard-code ctype attributes in the charset. */
	if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
		size_t csid_offset;
		assert(unicode_charset_isempty(&result->rc_uchars));
		bitset_foreach (csid_offset, result->rc_charclasses, CHARCLASS_COUNT) {
			uint8_t ctype_c_trait_mask;
			ctype_c_trait_mask = ctype_c_trait_masks[csid_offset];
			if (ctype_c_trait_mask != 0) {
				/* Can just copy attributes from `__ctype_C_flags' */
				unsigned int i;
do_copy_ctype_c_trait_mask:
				for (i = 0; i < 128; ++i) { /* 128 instead of 256, because non-ASCII is always `0' */
					if ((__ctype_C_flags[i] & ctype_c_trait_mask) != 0)
						bitset_set(result->rc_bytes, i);
				}
			} else {
				/* Need some custom handling (s.a. 0-cases in `ctype_c_trait_masks') */
				switch (csid_offset) {
				case RECS_ISBLANK - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x09);
					bitset_set(result->rc_bytes, 0x20);
					break;
				case RECS_ISSYMSTRT - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x24); /* '$' */
					bitset_set(result->rc_bytes, 0x5f); /* '_' */
					ctype_c_trait_mask = CTYPE_C_FLAG_ALPHA;
					goto do_copy_ctype_c_trait_mask;
				case RECS_ISSYMCONT - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x24); /* '$' */
					bitset_set(result->rc_bytes, 0x5f); /* '_' */
					ctype_c_trait_mask = CTYPE_C_FLAG_ALNUM;
					goto do_copy_ctype_c_trait_mask;
				case RECS_ISEMPTY - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x20); /* ' ' */
					ATTR_FALLTHROUGH
				case RECS_ISTAB - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x09); /* '\t' */
					bitset_set(result->rc_bytes, 0x0b); /* VT */
					bitset_set(result->rc_bytes, 0x0c); /* FF */
					break;
				case RECS_ISWHITE - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x20); /* ' ' */
					break;
				case RECS_ISLF - RECS_ISX_MIN:
					bitset_set(result->rc_bytes, 0x0a); /* '\n' */
					bitset_set(result->rc_bytes, 0x0d); /* '\r' */
					break;
				case RECS_ISHEX - RECS_ISX_MIN:
					bitset_nset_r(result->rc_bytes, 0x41, 0x46); /* 'A-F' */
					bitset_nset_r(result->rc_bytes, 0x61, 0x66); /* 'a-f' */
					break;
				default: __builtin_unreachable();
				}
			}
		}
	}

	/* In ICASE-mode, must merge the is-set state of 'A-Z' and 'a-z' in the ASCII area */
	if (IF_ICASE(self->rec_parser.rep_syntax)) {
		byte_t b;
		for (b = 0x41; b <= 0x5a; ++b) {
			if (bitset_test(result->rc_bytes, b) || bitset_test(result->rc_bytes, b | 0x20)) {
				bitset_set(result->rc_bytes, b);
				bitset_set(result->rc_bytes, b | 0x20);
			}
		}
	}

	/* Implicitly exclude line-feeds from the charset. */
	if (IF_HAT_LISTS_NOT_NEWLINE(self->rec_parser.rep_syntax) &&
	    (result->rc_negate)) {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			bitset_set(result->rc_bytes, 0x0a); /* '\n' */
			bitset_set(result->rc_bytes, 0x0d); /* '\r' */
		} else {
			bitset_set(result->rc_charclasses, RECS_ISLF - RECS_ISX_MIN);
		}
	}

	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif /* _MSC_VER */



PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_charset)(struct re_compiler *__restrict self) {
	re_errno_t result;
	struct re_charset cs;
	size_t start_offset; /* Set code start offset (offset of `REOP_[N]CS_*' opcode) */
	re_charset_init(&cs);

	/* Must make  space for  the leading  `REOP_[N]CS_*' opcode  now,  since
	 * parsing a character-set can immediately produce `RECS_RANGE' opcodes. */
	if unlikely(!re_compiler_require(self, 1)) /* Will need at least 1 byte */
		goto err_nomem;
	start_offset = (size_t)(self->rec_cpos - self->rec_cbase);
	++self->rec_cpos; /* Reserve space for leading `REOP_[N]CS_*' character. */

	/* Parse the charset */
	result = re_compiler_parse_charset(self, &cs);
	if unlikely(result != RE_NOERROR)
		goto done;

	/* Figure out how we want to represent everything. */
	if (cs.rc_negate) {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			self->rec_cbase[start_offset] = REOP_CS_BYTE;
			bitset_flipall(cs.rc_bytes, 256);
		} else {
			self->rec_cbase[start_offset] = REOP_NCS_UTF8;
			goto check_bytes_only_ascii;
		}
	} else {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			self->rec_cbase[start_offset] = REOP_CS_BYTE;
		} else if ((self->rec_cpos == self->rec_cbase + start_offset + 1) &&
		           (cs.rc_uchars.ucs_count == 0) &&
		           !bitset_anyset(cs.rc_charclasses, CHARCLASS_COUNT)) {
			/* Special case: even  when  compiling  in  utf-8-mode,  we  can still
			 *               encode char classes in their more efficient byte-mode
			 *               encoding,  so-long  as  the pattern  can  never match
			 *               a multi-byte utf-8 character! */
			self->rec_cbase[start_offset] = REOP_CS_BYTE;
		} else {
			self->rec_cbase[start_offset] = REOP_CS_UTF8;
check_bytes_only_ascii:
			/* Patterns like "[Ä\xC3]" are invalid, since you can't both
			 * match the singular byte "\xC3", as well as the utf-8 byte
			 * sequence "\xC3\x84" (well...  technically you could,  but
			 * not by using a charset)
			 *
			 * To keep things from escalating, we only allow ascii-chars
			 * being bitset-matched when encoding a utf-8 based charset. */
			if (bitset_nanyset_r(cs.rc_bytes, 0x80, 0xff))
				goto err_EILLSET;
		}
	}

	if (!(IF_NO_UTF8(self->rec_parser.rep_syntax))) {
		/* Encode `cs.rc_charclasses' (if in utf-8 mode) */
		size_t csid_offset;
		bitset_foreach (csid_offset, cs.rc_charclasses, CHARCLASS_COUNT) {
			byte_t cs_opcode = (byte_t)(RECS_ISX_MIN + csid_offset);
			if (!re_compiler_putc(self, cs_opcode))
				goto err_nomem;
			/* TODO: In non-cs.rc_negate-mode, try to clear the charset's bits from `bytes', so we don't
			 *       encode   matching  bytes  redundantly  (optimizes  "[[:hex:]a-f]"  ->  "[[:hex:]]") */
		}

		/* Encode `cs.rc_uchars' (if in utf-8 mode) */
		if (cs.rc_uchars.ucs_count > 0) {
			char *basep  = cs.rc_uchars.ucs_basep;
			size_t count = cs.rc_uchars.ucs_count;
			/* Special case: if the set contains only unicode chars, and
			 *               no  char-sets, and is  short enough to fit,
			 *               then encode as `REOP_[N]CONTAINS_UTF8' */
			if ((self->rec_cpos == self->rec_cbase + start_offset + 1)) {
				size_t nbytes = bitset_popcount(cs.rc_bytes, 256);
				if ((nbytes <= REOP_CONTAINS_UTF8_MAX_ASCII_COUNT) &&
				    ((count + nbytes) <= 0xff)) {
					/* Yes: we can use `REOP_[N]CONTAINS_UTF8'! */
					self->rec_cpos[-1] = cs.rc_negate ? (byte_t)REOP_NCONTAINS_UTF8
					                                  : (byte_t)REOP_CONTAINS_UTF8;
					if (!re_compiler_putc(self, (byte_t)(count + nbytes)))
						goto err_nomem;
					if (nbytes != 0) {
						/* Include ASCII characters in the CONTAINS-match. */
						byte_t b;
						for (b = 0; b < 0x80; ++b) {
							if (bitset_test(cs.rc_bytes, b)) {
								if (!re_compiler_putc(self, b))
									goto err_nomem;
							}
						}
					}
					if (!re_compiler_putn(self, basep, (size_t)(cs.rc_uchars.ucs_endp - basep)))
						goto err_nomem;
					goto done;
				}
			}

			while (count > 0xff) {
				/* Must encode in multiple sets. */
				char *new_basep;
				new_basep = utf8_skipn(basep, 255);
				if (!re_compiler_putc(self, RECS_CONTAINS))
					goto err_nomem;
				if (!re_compiler_putc(self, 255))
					goto err_nomem;
				if (!re_compiler_putn(self, basep, (size_t)(new_basep - basep)))
					goto err_nomem;
				basep = new_basep;
				count -= 0xff;
			}
			if (count == 2) {
				if (!re_compiler_putc(self, RECS_CHAR2))
					goto err_nomem;
			} else if (count == 1) {
				if (!re_compiler_putc(self, RECS_CHAR))
					goto err_nomem;
			} else {
				if (!re_compiler_putc(self, RECS_CONTAINS))
					goto err_nomem;
				if (!re_compiler_putc(self, (byte_t)count))
					goto err_nomem;
			}
			if (!re_compiler_putn(self, basep, (size_t)(cs.rc_uchars.ucs_endp - basep)))
				goto err_nomem;
		}
	}

	/* Encode `bytes' (with optimizations in  case no other prefixes were  written
	 * yet, iow: when `self->rec_cpos == self->rec_cbase + start_offset', in which
	 * case try to encode as `REOP_[N]BYTE', `REOP_[N]BYTE2' or `REOP_[N]RANGE') */
	if (self->rec_cpos == self->rec_cbase + start_offset) {
		/* Nothing out of the ordinary happened, yet.
		 * -> Check if we can encode `bytes' as one of:
		 *    - REOP_[N]BYTE
		 *    - REOP_[N]BYTE2
		 *    - REOP_[N]RANGE
		 */
		unsigned int b;
		unsigned int popcount;
		byte_t range_lo, range_hi;
		for (b = 0;; ++b) {
			if (b >= 256)
				goto put_terminator; /* Empty bytes? -- ok... (but this won't ever match anything) */
			if (bitset_test(cs.rc_bytes, b))
				break;
		}
		/* Found the first matching character.
		 * -> check if we can encode this via `REOP_[N]RANGE'. */
		popcount = 1;
		range_lo = (byte_t)b;
		range_hi = (byte_t)b;
		while (range_hi < (256 - 1) && bitset_test(cs.rc_bytes, range_hi + 1)) {
			++range_hi;
			++popcount;
		}
		if (range_hi >= 0xff || !bitset_nanyset_r(cs.rc_bytes, range_hi + 1, 0xff)) {
			/* We're dealing with a singular, continuous range [range_lo,range_hi] */
			unsigned int rangelen = (range_hi - range_lo) + 1;
			assert(rangelen >= 1);
			if (rangelen == 1) {
				/* The entire set matches only a single byte, specific. */
				assert(range_lo == range_hi);
				self->rec_cpos[-1] = cs.rc_negate ? (byte_t)REOP_NBYTE
				                                  : (byte_t)REOP_BYTE;
				if (!re_compiler_putc(self, range_lo))
					goto err_nomem;
				goto done;
			} else if (rangelen == 2) {
				/* The entire set matches only 2 different bytes. */
				assert(range_lo + 1 == range_hi);
				self->rec_cpos[-1] = cs.rc_negate ? (byte_t)REOP_NBYTE2
				                                  : (byte_t)REOP_BYTE2;
				if (!re_compiler_putc(self, range_lo))
					goto err_nomem;
				if (!re_compiler_putc(self, range_hi))
					goto err_nomem;
				goto done;
			} else {
				/* The entire set matches only a specific range of bytes */
				assert(range_lo + 1 == range_hi);
				self->rec_cpos[-1] = cs.rc_negate ? (byte_t)REOP_NRANGE
				                                  : (byte_t)REOP_RANGE;
				if (!re_compiler_putc(self, range_lo))
					goto err_nomem;
				if (!re_compiler_putc(self, range_hi))
					goto err_nomem;
				goto done;
			}
			goto done;
		}
		if (popcount == 1) {
			/* Check if we can find another matching byte somewhere `> b' */
			b += 2;                              /* b+1 was already checked, so start at b += 2 */
			while (!bitset_test(cs.rc_bytes, b)) /* We know that there must be more set bits, because `!bitset_nanyset' */
				++b;
			/* At this point, we know that `range_lo' and `b' are part of the set.
			 * -> If these are the only 2, then we can generate `REOP_[N]BYTE2'. */
			if (b >= 0xff || !bitset_nanyset_r(cs.rc_bytes, b + 1, 0xff)) {
				self->rec_cpos[-1] = cs.rc_negate ? (byte_t)REOP_NBYTE2
				                                  : (byte_t)REOP_BYTE2;
				if (!re_compiler_putc(self, range_lo))
					goto err_nomem;
				if (!re_compiler_putc(self, (byte_t)b))
					goto err_nomem;
				goto done;
			}
		}
	}

	/* Check if there are even any bytes _to_ encode. */
	if unlikely(!bitset_anyset(cs.rc_bytes, 256)) {
		/* No byte-matches specified -> don't encode a bitset.
		 * - This can easily happen for (e.g.) "[[:alpha:]]" in utf-8 mode,
		 *   where it  encodes  to  `REOP_CS_UTF8, RECS_ISALPHA, RECS_DONE'
		 */
		goto put_terminator;
	}

	/* No special encoding is possible -> must instead use `RECS_BITSET_MIN'! */
	{
		byte_t cs_opcode;
		byte_t minset, maxset, base, num_bytes;
		uint16_t num_bits;
		minset    = (byte_t)(0x00 + bitset_rawctz(cs.rc_bytes));
		maxset    = (byte_t)(0xff - bitset_rawclz(cs.rc_bytes, 256));
		base      = RECS_BITSET_BASEFOR(minset);
		num_bits  = (maxset + 1) - base;
		num_bytes = (byte_t)CEILDIV(num_bits, 8);
		assertf(num_bytes <= 0x20, "%" PRIuSIZ, num_bytes);
		cs_opcode = RECS_BITSET_BUILD(base, num_bytes);
		assertf((cs_opcode <= RECS_BITSET_MAX_UTF8) ||
		        (self->rec_cbase[start_offset] == REOP_CS_BYTE),
		        "This should have been asserted by the err_EILLSET check above");
		if (!re_compiler_putc(self, cs_opcode))
			goto err_nomem;
		static_assert(sizeof(*cs.rc_bytes) == sizeof(byte_t));
		if (!re_compiler_putn(self, cs.rc_bytes + (base / 8), num_bytes))
			goto err_nomem;
	} /* scope... */

	/* Terminate the generated character-set */
put_terminator:
	if (!re_compiler_putc(self, RECS_DONE))
		goto err_nomem;
done:
	re_charset_fini(&cs);
	return result;
err_nomem:
	result = RE_ESPACE;
	goto done;
err_EILLSET:
	result = RE_EILLSET;
	goto done;
}

/* Special return value for `re_compiler_compile_prefix':
 * the prefix has  just concluded with  `REOP_GROUP_END',
 * or `REOP_GROUP_MATCH' opcode, but the referenced group
 * is capable of matching epsilon! */
#define RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP (-1)

/* Compile prefix expressions: literals, '[...]' and '(...)'
 * This function also sets `self->rec_estart' */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_prefix)(struct re_compiler *__restrict self) {
	re_token_t tok;
	char const *tokstart;
#if ALTERNATION_PREFIX_MAXLEN > 0
	byte_t alternation_prefix[ALTERNATION_PREFIX_MAXLEN];
	size_t alternation_prefix_len;
	bool alternation_prefix_wanted;
#define alternation_prefix_hasspace(num_bytes) ((ALTERNATION_PREFIX_MAXLEN - alternation_prefix_len) >= (num_bytes))
#define alternation_prefix_putc(byte)       (void)(alternation_prefix[alternation_prefix_len++] = (byte))
#define alternation_prefix_putn(p, n)       (void)(memcpy(&alternation_prefix[alternation_prefix_len], p, n), alternation_prefix_len += (n))
#define alternation_prefix_dump()                                                    \
	do {                                                                             \
		if (alternation_prefix_len > 0) {                                            \
			if (!re_compiler_putn(self, alternation_prefix, alternation_prefix_len)) \
				goto err_nomem;                                                      \
			alternation_prefix_wanted = false;                                       \
		}                                                                            \
	}	__WHILE0
#else /* ALTERNATION_PREFIX_MAXLEN > 0 */
#define alternation_prefix_hasspace(num_bytes) 0
#define alternation_prefix_dump() (void)0
#endif /* ALTERNATION_PREFIX_MAXLEN <= 0 */


	/* Check if we want to produce alternation prefixes. */
#if ALTERNATION_PREFIX_MAXLEN > 0
	assert(self->rec_cpos >= self->rec_code->rc_code);
	alternation_prefix_wanted = self->rec_cpos <= self->rec_code->rc_code;
	alternation_prefix_len    = 0;
#endif /* ALTERNATION_PREFIX_MAXLEN > 0 */
again:
	self->rec_estart = self->rec_cpos; /* Start of new (sub-)expression */

	/* Start parsing the current expression */
	tokstart = self->rec_parser.rep_pos;
	tok      = re_compiler_yield(self);
	switch (tok) {

		/* Tokens which we don't consume, but instead treat as an epsilon-match */
	case RE_TOKEN_EOF:         /* End of pattern */
	case RE_TOKEN_ENDGROUP:    /* ')'-token (let the caller deal with this) */
	case RE_TOKEN_ALTERNATION: /* '|'-token (let the caller deal with this) */
		self->rec_parser.rep_pos = tokstart;
		return RE_NOERROR;

	case RE_TOKEN_UNMATCHED_BK: /* Unmatched '\' */
		return RE_EESCAPE;

	case RE_TOKEN_ILLSEQ: /* Illegal unicode sequence */
		return RE_EILLSEQ;

	case RE_TOKEN_STARTINTERVAL:
		if (IF_CONTEXT_INVALID_DUP(self->rec_parser.rep_syntax) ||
		    IF_CONTEXT_INVALID_OPS(self->rec_parser.rep_syntax))
			return RE_BADRPT;
		tok = '{';
		goto do_literal;

	case RE_TOKEN_PLUS:
		if (IF_CONTEXT_INVALID_OPS(self->rec_parser.rep_syntax))
			return RE_BADRPT;
		tok = '+';
		goto do_literal;

	case RE_TOKEN_STAR:
		if (IF_CONTEXT_INVALID_OPS(self->rec_parser.rep_syntax))
			return RE_BADRPT;
		tok = '*';
		goto do_literal;

	case RE_TOKEN_QMARK:
		if (IF_CONTEXT_INVALID_OPS(self->rec_parser.rep_syntax))
			return RE_BADRPT;
		tok = '?';
		goto do_literal;

	case RE_TOKEN_STARTGROUP: {
		/* Group and parenthesis */
		bool group_matches_epsilon;
		size_t expr_start_offset;
		size_t group_start_offset;
		re_errno_t error;
		uint8_t gid;
		byte_t *body;
#ifdef RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD
		uintptr_t old_syntax;
#endif /* RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD */

		if unlikely(self->rec_code->rc_ngrps >= 0x100)
			return RE_ESIZE; /* Too many groups */
		gid = (byte_t)(self->rec_code->rc_ngrps++);

		/* We're inside of a group, so ')' are no longer literals! */
#ifdef RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD
		old_syntax = self->rec_parser.rep_syntax;
		self->rec_parser.rep_syntax &= ~RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD;
#endif /* RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD */

		/* Optimization: "(a|b)" normally compiles as:
		 * >> 0x0000:    REOP_GROUP_START 0
		 * >> 0x0002:    REOP_JMP_ONFAIL  1f
		 * >> 0x0005:    REOP_BYTE        "a"
		 * >> 0x0007:    REOP_JMP         2f
		 * >> 0x000a: 1: REOP_BYTE        "b"
		 * >> 0x000c: 2: REOP_GROUP_END   0
		 * With a fast-map: ["ab": @0x0000]
		 *
		 * However, if we compiled it like this:
		 * >> 0x0000:    REOP_JMP_ONFAIL  1f
		 * >> 0x0003:    REOP_GROUP_START 0
		 * >> 0x0005:    REOP_BYTE        "a"
		 * >> 0x0007:    REOP_JMP         2f
		 * >> 0x000a: 1: REOP_GROUP_START 0
		 * >> 0x000c:    REOP_BYTE        "b"
		 * >> 0x000e: 2: REOP_GROUP_END   0
		 * Then the fastmap would be compiled as:
		 *    - "a": @0x0003
		 *    - "b": @0x000a
		 *
		 * NOTE: Only do this if the base-expression starts with
		 *       a  group that contains  at least 1 alternation! */
		expr_start_offset = (size_t)(self->rec_estart - self->rec_cbase);
#if ALTERNATION_PREFIX_MAXLEN > 0
		if (alternation_prefix_wanted) {
			if (!alternation_prefix_hasspace(2)) {
				alternation_prefix_dump();
				goto do_group_start_without_alternation;
			}
			alternation_prefix_putc(REOP_GROUP_START);
			alternation_prefix_putc(gid);
		} else
#endif /* ALTERNATION_PREFIX_MAXLEN > 0 */
		{
			/* Generate the introductory `REOP_GROUP_START' instruction */
#if ALTERNATION_PREFIX_MAXLEN > 0
do_group_start_without_alternation:
#endif /* ALTERNATION_PREFIX_MAXLEN > 0 */
			if (!re_compiler_putc(self, REOP_GROUP_START))
				goto err_nomem;
			if (!re_compiler_putc(self, gid))
				goto err_nomem;
		}

		/* Compile the actual contents of the group. */
		group_start_offset = (size_t)(self->rec_cpos - self->rec_cbase);
		error = re_compiler_compile_alternation(self, alternation_prefix, alternation_prefix_len);
		if unlikely(error != RE_NOERROR)
			return error;

		/* Consume the trailing ')'-token */
		tok = re_compiler_yield(self);
		if unlikely(tok != RE_TOKEN_ENDGROUP) {
			if (RE_TOKEN_ISERROR(tok))
				return RE_TOKEN_GETERROR(tok);
			return RE_EPAREN;
		}

		/* Restore old syntax behavior for  ')' being a literal or  not.
		 * s.a. us clearing `RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD' above. */
#ifdef RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD
		self->rec_parser.rep_syntax = old_syntax;
#endif /* RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD */

		/* Mark the end of the group (this opcode may be overwritten later) */
		if (!re_compiler_putc(self, REOP_GROUP_END))
			goto err_nomem;
		if (!re_compiler_putc(self, gid))
			goto err_nomem;

		/* Figure out if the group is able to match EPSION */
		if unlikely(!re_compiler_require(self, 1))
			goto err_nomem;
		*self->rec_cpos = REOP_MATCHED_PERFECT;
		body = self->rec_cbase + group_start_offset;
		group_matches_epsilon = re_code_matches_epsilon(body);

		/* Restore the expression-start offset to point at the `REOP_GROUP_START' opcode. */
		self->rec_estart = self->rec_cbase + expr_start_offset;

		/* Remember information about the first 9 groups */
		if (gid < lengthof(self->rec_grpinfo)) {
			self->rec_grpinfo[gid] |= RE_COMPILER_GRPINFO_DEFINED;
			if (group_matches_epsilon)
				self->rec_grpinfo[gid] |= RE_COMPILER_GRPINFO_EPSILON;
		}
		if (group_matches_epsilon)
			return RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP;
		return RE_NOERROR;
	}

	case RE_TOKEN_STARTSET:
		alternation_prefix_dump();
		return re_compiler_compile_charset(self);

	case RE_TOKEN_BK_w:
	case RE_TOKEN_BK_W: {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			static byte_t const issymcont_y_code[] = {
				REOP_CS_BYTE,
				RECS_BITSET_BUILD(0x20, 12),
				/* 20-27 */ 0x00 | (1 << (0x24 % 8)), /* $ */
				/* 28-2F */ 0x00,
				/* 30-37 */ 0x00 | (1 << (0x30 % 8))  /* 0 */
				/*            */ | (1 << (0x31 % 8))  /* 1 */
				/*            */ | (1 << (0x32 % 8))  /* 2 */
				/*            */ | (1 << (0x33 % 8))  /* 3 */
				/*            */ | (1 << (0x34 % 8))  /* 4 */
				/*            */ | (1 << (0x35 % 8))  /* 5 */
				/*            */ | (1 << (0x36 % 8))  /* 6 */
				/*            */ | (1 << (0x37 % 8)), /* 7 */
				/* 38-3F */ 0x00 | (1 << (0x38 % 8))  /* 8 */
				/*            */ | (1 << (0x39 % 8)), /* 9 */
				/* 40-47 */ 0x00 | (1 << (0x41 % 8))  /* A */
				/*            */ | (1 << (0x42 % 8))  /* B */
				/*            */ | (1 << (0x43 % 8))  /* C */
				/*            */ | (1 << (0x44 % 8))  /* D */
				/*            */ | (1 << (0x45 % 8))  /* E */
				/*            */ | (1 << (0x46 % 8))  /* F */
				/*            */ | (1 << (0x47 % 8)), /* G */
				/* 48-4F */ 0x00 | (1 << (0x48 % 8))  /* H */
				/*            */ | (1 << (0x49 % 8))  /* I */
				/*            */ | (1 << (0x4a % 8))  /* J */
				/*            */ | (1 << (0x4b % 8))  /* K */
				/*            */ | (1 << (0x4c % 8))  /* L */
				/*            */ | (1 << (0x4d % 8))  /* M */
				/*            */ | (1 << (0x4e % 8))  /* N */
				/*            */ | (1 << (0x4f % 8)), /* O */
				/* 50-57 */ 0x00 | (1 << (0x50 % 8))  /* P */
				/*            */ | (1 << (0x51 % 8))  /* Q */
				/*            */ | (1 << (0x52 % 8))  /* R */
				/*            */ | (1 << (0x53 % 8))  /* S */
				/*            */ | (1 << (0x54 % 8))  /* T */
				/*            */ | (1 << (0x55 % 8))  /* U */
				/*            */ | (1 << (0x56 % 8))  /* V */
				/*            */ | (1 << (0x57 % 8)), /* W */
				/* 58-5F */ 0x00 | (1 << (0x58 % 8))  /* X */
				/*            */ | (1 << (0x59 % 8))  /* Y */
				/*            */ | (1 << (0x5a % 8))  /* Z */
				/*            */ | (1 << (0x5f % 8)), /* _ */
				/* 60-67 */ 0x00 | (1 << (0x61 % 8))  /* a */
				/*            */ | (1 << (0x62 % 8))  /* b */
				/*            */ | (1 << (0x63 % 8))  /* c */
				/*            */ | (1 << (0x64 % 8))  /* d */
				/*            */ | (1 << (0x65 % 8))  /* e */
				/*            */ | (1 << (0x66 % 8))  /* f */
				/*            */ | (1 << (0x67 % 8)), /* g */
				/* 68-6F */ 0x00 | (1 << (0x68 % 8))  /* h */
				/*            */ | (1 << (0x69 % 8))  /* i */
				/*            */ | (1 << (0x6a % 8))  /* j */
				/*            */ | (1 << (0x6b % 8))  /* k */
				/*            */ | (1 << (0x6c % 8))  /* l */
				/*            */ | (1 << (0x6d % 8))  /* m */
				/*            */ | (1 << (0x6e % 8))  /* n */
				/*            */ | (1 << (0x6f % 8)), /* o */
				/* 70-77 */ 0x00 | (1 << (0x70 % 8))  /* p */
				/*            */ | (1 << (0x71 % 8))  /* q */
				/*            */ | (1 << (0x72 % 8))  /* r */
				/*            */ | (1 << (0x73 % 8))  /* s */
				/*            */ | (1 << (0x74 % 8))  /* t */
				/*            */ | (1 << (0x75 % 8))  /* u */
				/*            */ | (1 << (0x76 % 8))  /* v */
				/*            */ | (1 << (0x77 % 8)), /* w */
				/* 78-7F */ 0x00 | (1 << (0x78 % 8))  /* x */
				/*            */ | (1 << (0x79 % 8))  /* y */
				/*            */ | (1 << (0x7a % 8)), /* z */
				RECS_DONE
			};
			static byte_t const issymcont_n_code[] = {
				REOP_CS_BYTE,
				RECS_BITSET_BUILD(0x00, 32),
				/* 00-0F */ 0xff, 0xff,
				/* 10-1F */ 0xff, 0xff,
				/* 20-27 */ 0xff & ~(1 << (0x24 % 8)), /* $ */
				/* 28-2F */ 0xff,
				/* 30-37 */ 0xff & ~(1 << (0x30 % 8))  /* 0 */
				/*            */ & ~(1 << (0x31 % 8))  /* 1 */
				/*            */ & ~(1 << (0x32 % 8))  /* 2 */
				/*            */ & ~(1 << (0x33 % 8))  /* 3 */
				/*            */ & ~(1 << (0x34 % 8))  /* 4 */
				/*            */ & ~(1 << (0x35 % 8))  /* 5 */
				/*            */ & ~(1 << (0x36 % 8))  /* 6 */
				/*            */ & ~(1 << (0x37 % 8)), /* 7 */
				/* 38-3F */ 0xff & ~(1 << (0x38 % 8))  /* 8 */
				/*            */ & ~(1 << (0x39 % 8)), /* 9 */
				/* 40-47 */ 0xff & ~(1 << (0x41 % 8))  /* A */
				/*            */ & ~(1 << (0x42 % 8))  /* B */
				/*            */ & ~(1 << (0x43 % 8))  /* C */
				/*            */ & ~(1 << (0x44 % 8))  /* D */
				/*            */ & ~(1 << (0x45 % 8))  /* E */
				/*            */ & ~(1 << (0x46 % 8))  /* F */
				/*            */ & ~(1 << (0x47 % 8)), /* G */
				/* 48-4F */ 0xff & ~(1 << (0x48 % 8))  /* H */
				/*            */ & ~(1 << (0x49 % 8))  /* I */
				/*            */ & ~(1 << (0x4a % 8))  /* J */
				/*            */ & ~(1 << (0x4b % 8))  /* K */
				/*            */ & ~(1 << (0x4c % 8))  /* L */
				/*            */ & ~(1 << (0x4d % 8))  /* M */
				/*            */ & ~(1 << (0x4e % 8))  /* N */
				/*            */ & ~(1 << (0x4f % 8)), /* O */
				/* 50-57 */ 0xff & ~(1 << (0x50 % 8))  /* P */
				/*            */ & ~(1 << (0x51 % 8))  /* Q */
				/*            */ & ~(1 << (0x52 % 8))  /* R */
				/*            */ & ~(1 << (0x53 % 8))  /* S */
				/*            */ & ~(1 << (0x54 % 8))  /* T */
				/*            */ & ~(1 << (0x55 % 8))  /* U */
				/*            */ & ~(1 << (0x56 % 8))  /* V */
				/*            */ & ~(1 << (0x57 % 8)), /* W */
				/* 58-5F */ 0xff & ~(1 << (0x58 % 8))  /* X */
				/*            */ & ~(1 << (0x59 % 8))  /* Y */
				/*            */ & ~(1 << (0x5a % 8))  /* Z */
				/*            */ & ~(1 << (0x5f % 8)), /* _ */
				/* 60-67 */ 0xff & ~(1 << (0x61 % 8))  /* a */
				/*            */ & ~(1 << (0x62 % 8))  /* b */
				/*            */ & ~(1 << (0x63 % 8))  /* c */
				/*            */ & ~(1 << (0x64 % 8))  /* d */
				/*            */ & ~(1 << (0x65 % 8))  /* e */
				/*            */ & ~(1 << (0x66 % 8))  /* f */
				/*            */ & ~(1 << (0x67 % 8)), /* g */
				/* 68-6F */ 0xff & ~(1 << (0x68 % 8))  /* h */
				/*            */ & ~(1 << (0x69 % 8))  /* i */
				/*            */ & ~(1 << (0x6a % 8))  /* j */
				/*            */ & ~(1 << (0x6b % 8))  /* k */
				/*            */ & ~(1 << (0x6c % 8))  /* l */
				/*            */ & ~(1 << (0x6d % 8))  /* m */
				/*            */ & ~(1 << (0x6e % 8))  /* n */
				/*            */ & ~(1 << (0x6f % 8)), /* o */
				/* 70-77 */ 0xff & ~(1 << (0x70 % 8))  /* p */
				/*            */ & ~(1 << (0x71 % 8))  /* q */
				/*            */ & ~(1 << (0x72 % 8))  /* r */
				/*            */ & ~(1 << (0x73 % 8))  /* s */
				/*            */ & ~(1 << (0x74 % 8))  /* t */
				/*            */ & ~(1 << (0x75 % 8))  /* u */
				/*            */ & ~(1 << (0x76 % 8))  /* v */
				/*            */ & ~(1 << (0x77 % 8)), /* w */
				/* 78-7F */ 0xff & ~(1 << (0x78 % 8))  /* x */
				/*            */ & ~(1 << (0x79 % 8))  /* y */
				/*            */ & ~(1 << (0x7a % 8)), /* z */
				/* 80-8F */ 0xff, 0xff,
				/* 90-9F */ 0xff, 0xff,
				/* A0-AF */ 0xff, 0xff,
				/* B0-BF */ 0xff, 0xff,
				/* C0-CF */ 0xff, 0xff,
				/* D0-DF */ 0xff, 0xff,
				/* E0-EF */ 0xff, 0xff,
				/* F0-FF */ 0xff, 0xff,
				RECS_DONE
			};
			if (tok == RE_TOKEN_BK_W) {
				if unlikely(!re_compiler_putn(self, issymcont_n_code, sizeof(issymcont_n_code)))
					goto err_nomem;
			} else {
				if unlikely(!re_compiler_putn(self, issymcont_y_code, sizeof(issymcont_y_code)))
					goto err_nomem;
			}
		} else {
			if (!re_compiler_putc(self, tok == RE_TOKEN_BK_W ? (byte_t)REOP_NCS_UTF8
			                                                 : (byte_t)REOP_CS_UTF8))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_ISSYMCONT))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_DONE))
				goto err_nomem;
		}
		goto done_prefix;
	}

	case RE_TOKEN_BK_s:
	case RE_TOKEN_BK_S: {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			static byte_t const isspace_y_code[] = {
				REOP_CS_BYTE,
				RECS_BITSET_BUILD(0x00, 5),
				/* 00-07 */ 0x00,
				/* 08-0F */ 0x00 | (1 << (0x09 % 8))
				/*            */ | (1 << (0x0A % 8))
				/*            */ | (1 << (0x0B % 8))
				/*            */ | (1 << (0x0C % 8))
				/*            */ | (1 << (0x0D % 8)),
				/* 10-17 */ 0x00,
				/* 18-1F */ 0x00,
				/* 20-27 */ 0x00 | (1 << (0x20 % 8)),
				RECS_DONE
			};
			static byte_t const isspace_n_code[] = {
				REOP_CS_BYTE,
				RECS_BITSET_BUILD(0x00, 32),
				/* 00-0F */ 0xff, 0xff & ~(1 << (0x09 % 8))
				/*                  */ & ~(1 << (0x0A % 8))
				/*                  */ & ~(1 << (0x0B % 8))
				/*                  */ & ~(1 << (0x0C % 8))
				/*                  */ & ~(1 << (0x0D % 8)),
				/* 10-1F */ 0xff, 0xff,
				/* 20-2F */ 0xff & ~(1 << (0x20 % 8)), 0xff,
				/* 30-3F */ 0xff, 0xff,
				/* 40-4F */ 0xff, 0xff,
				/* 50-5F */ 0xff, 0xff,
				/* 60-6F */ 0xff, 0xff,
				/* 70-7F */ 0xff, 0xff,
				/* 80-8F */ 0xff, 0xff,
				/* 90-9F */ 0xff, 0xff,
				/* A0-AF */ 0xff, 0xff,
				/* B0-BF */ 0xff, 0xff,
				/* C0-CF */ 0xff, 0xff,
				/* D0-DF */ 0xff, 0xff,
				/* E0-EF */ 0xff, 0xff,
				/* F0-FF */ 0xff, 0xff,
				RECS_DONE
			};
			if (tok == RE_TOKEN_BK_W) {
				if unlikely(!re_compiler_putn(self, isspace_n_code, sizeof(isspace_n_code)))
					goto err_nomem;
			} else {
				if unlikely(!re_compiler_putn(self, isspace_y_code, sizeof(isspace_y_code)))
					goto err_nomem;
			}
		} else {
			if (!re_compiler_putc(self, (tok == RE_TOKEN_BK_W) ? (byte_t)REOP_NCS_UTF8
			                                                   : (byte_t)REOP_CS_UTF8))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_ISSPACE))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_DONE))
				goto err_nomem;
		}
		goto done_prefix;
	}

	case RE_TOKEN_BK_d:
	case RE_TOKEN_BK_D: {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			if (!re_compiler_putc(self, (tok == RE_TOKEN_BK_D) ? (byte_t)REOP_NRANGE
			                                                   : (byte_t)REOP_RANGE))
				goto err_nomem;
			if (!re_compiler_putc(self, (byte_t)0x30)) /* '0' */
				goto err_nomem;
			if (!re_compiler_putc(self, (byte_t)0x39)) /* '9' */
				goto err_nomem;
		} else {
			if (!re_compiler_putc(self, (tok == RE_TOKEN_BK_D) ? (byte_t)REOP_NCS_UTF8
			                                                   : (byte_t)REOP_CS_UTF8))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_ISDIGIT))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_DONE))
				goto err_nomem;
		}
		goto done_prefix;
	}

	case RE_TOKEN_BK_n:
	case RE_TOKEN_BK_N: {
		if (IF_NO_UTF8(self->rec_parser.rep_syntax)) {
			if (!re_compiler_putc(self, (tok == RE_TOKEN_BK_N) ? (byte_t)REOP_NBYTE2
			                                                   : (byte_t)REOP_BYTE2))
				goto err_nomem;
			if (!re_compiler_putc(self, (byte_t)0x0a)) /* '\n' */
				goto err_nomem;
			if (!re_compiler_putc(self, (byte_t)0x0d)) /* '\r' */
				goto err_nomem;
		} else {
			if (!re_compiler_putc(self, (tok == RE_TOKEN_BK_N) ? (byte_t)REOP_NCS_UTF8
			                                                   : (byte_t)REOP_CS_UTF8))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_ISLF))
				goto err_nomem;
			if (!re_compiler_putc(self, RECS_DONE))
				goto err_nomem;
		}
		goto done_prefix;
	}

	case RE_TOKEN_ANY: {
		uint8_t opcode;
#define _ANY_CASE_XCLAIM_IF_NOT_WANTED_0 !
#define _ANY_CASE_XCLAIM_IF_NOT_WANTED_1 /* nothing */
#define _ANY_CASE_XCLAIM_IF_NOT_WANTED(x) _ANY_CASE_XCLAIM_IF_NOT_WANTED_##x
#define ANY_CASE(opcode_, want_nul, want_lf, want_utf8)                                                \
		if (_ANY_CASE_XCLAIM_IF_NOT_WANTED(want_nul)  !IF_DOT_NOT_NULL(self->rec_parser.rep_syntax) && \
		    _ANY_CASE_XCLAIM_IF_NOT_WANTED(want_lf)    IF_DOT_NEWLINE(self->rec_parser.rep_syntax) &&  \
		    _ANY_CASE_XCLAIM_IF_NOT_WANTED(want_utf8) !IF_NO_UTF8(self->rec_parser.rep_syntax)) {      \
			opcode = opcode_;                                                                          \
		} else
#ifdef REOP_ANY_NOTNUL_NOTLF
		ANY_CASE(REOP_ANY_NOTNUL_NOTLF /**/, 0, 0, 0)
#endif /* REOP_ANY_NOTNUL_NOTLF  */
#ifdef REOP_ANY_NOTNUL_NOTLF_UTF8
		ANY_CASE(REOP_ANY_NOTNUL_NOTLF_UTF8, 0, 0, 1)
#endif /* REOP_ANY_NOTNUL_NOTLF_UTF8 */
#ifdef REOP_ANY_NOTNUL
		ANY_CASE(REOP_ANY_NOTNUL /*      */, 0, 1, 0)
#endif /* REOP_ANY_NOTNUL  */
#ifdef REOP_ANY_NOTNUL_UTF8
		ANY_CASE(REOP_ANY_NOTNUL_UTF8 /* */, 0, 1, 1)
#endif /* REOP_ANY_NOTNUL_UTF8  */
#ifdef REOP_ANY_NOTLF
		ANY_CASE(REOP_ANY_NOTLF /*       */, 1, 0, 0)
#endif /* REOP_ANY_NOTLF  */
#ifdef REOP_ANY_NOTLF_UTF8
		ANY_CASE(REOP_ANY_NOTLF_UTF8 /*  */, 1, 0, 1)
#endif /* REOP_ANY_NOTLF_UTF8  */
#ifdef REOP_ANY
		ANY_CASE(REOP_ANY /*             */, 1, 1, 0)
#endif /* REOP_ANY  */
#ifdef REOP_ANY_UTF8
		ANY_CASE(REOP_ANY_UTF8 /*        */, 1, 1, 1)
#endif /* REOP_ANY_UTF8  */
		{
			__builtin_unreachable();
		}
#undef ANY_CASE
#undef _ANY_CASE_XCLAIM_IF_NOT_WANTED
#undef _ANY_CASE_XCLAIM_IF_NOT_WANTED_1
#undef _ANY_CASE_XCLAIM_IF_NOT_WANTED_0

		alternation_prefix_dump();
		if (!re_compiler_putc(self, opcode))
			goto err_nomem;
		goto done_prefix;
	}

	case_RE_TOKEN_AT_MIN_to_MAX: {
		static uint8_t const at_opcodes[(RE_TOKEN_AT_MAX - RE_TOKEN_AT_MIN) + 1][2] = {
#ifdef __GNUC__
#define DEF_OPCODE_PAIR(token, op, op_utf8) [(token - RE_TOKEN_AT_MIN)] = { op, op_utf8 }
#else /* __GNUC__ */
#define DEF_OPCODE_PAIR(token, op, op_utf8) /*[(token - RE_TOKEN_AT_MIN)] =*/ { op, op_utf8 }
#endif /* !__GNUC__ */
			DEF_OPCODE_PAIR(RE_TOKEN_AT_SOL, REOP_AT_SOI, REOP_AT_SOI),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_EOL, REOP_AT_EOI, REOP_AT_EOI),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_SOI, REOP_AT_SOL, REOP_AT_SOL_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_EOI, REOP_AT_EOL, REOP_AT_EOL_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_WOB, REOP_AT_WOB, REOP_AT_WOB_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_WOB_NOT, REOP_AT_WOB_NOT, REOP_AT_WOB_UTF8_NOT),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_SOW, REOP_AT_SOW, REOP_AT_SOW_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_EOW, REOP_AT_EOW, REOP_AT_EOW_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_SOS, REOP_AT_SOS, REOP_AT_SOS_UTF8),
			DEF_OPCODE_PAIR(RE_TOKEN_AT_EOS, REOP_AT_EOS, REOP_AT_EOS_UTF8),
#undef DEF_OPCODE_PAIR
		};
		uint8_t opcode = at_opcodes[tok - RE_TOKEN_AT_MIN]
		                           [(IF_NO_UTF8(self->rec_parser.rep_syntax)) ? 0 : 1];

		/* Deal with special handling for '^' and '$' */
		if (REOP_AT_SOLEOL_CHECK(opcode) && !IF_ANCHORS_IGNORE_EFLAGS(self->rec_parser.rep_syntax))
			opcode = REOP_AT_SOLEOL_MAKEX(opcode);

		/* `REOP_AT_*' qualify for being written as alternation prefixes! */
#if ALTERNATION_PREFIX_MAXLEN > 0
		if (alternation_prefix_wanted) {
			if (alternation_prefix_hasspace(1)) {
				alternation_prefix_putc(opcode);
				/* Parse another prefix expression since location assertions don't count as prefixes! */
				goto again;
			} else {
				alternation_prefix_dump();
			}
		}
#endif /* ALTERNATION_PREFIX_MAXLEN > 0 */

		if (!re_compiler_putc(self, opcode))
			goto err_nomem;
		/* Parse another prefix expression since location assertions don't count as prefixes! */
		goto again;
	}

	case_RE_TOKEN_BKREF_1_to_9: {
		uint8_t gid = (uint8_t)(tok - RE_TOKEN_BKREF_1);
		uint8_t ginfo;
		assert(gid <= 8); /* 1-9 --> 9 groups -> 0-based index must be <= 8 */
		ginfo = self->rec_grpinfo[gid];
		if (!(ginfo & RE_COMPILER_GRPINFO_DEFINED))
			return RE_ESUBREG;
		alternation_prefix_dump();
		self->rec_code->rc_flags |= RE_CODE_FLAG_NEEDGROUPS;
		if (!re_compiler_putc(self, REOP_GROUP_MATCH))
			goto err_nomem;
		if (!re_compiler_putc(self, gid))
			goto err_nomem;
		if (ginfo & RE_COMPILER_GRPINFO_EPSILON)
			return RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP;
		goto done_prefix;
	}

	default:
		if (!RE_TOKEN_ISLITERAL(tok)) {
			return RE_BADPAT;
		} else {
			bool literal_seq_hasesc;
			bool literal_seq_isutf8;
#ifndef __OPTIMIZE_SIZE__
			bool seq_followed_by_suffix;
#endif /* !__OPTIMIZE_SIZE__ */
			char const *literal_seq_start;
			char const *literal_seq_end;
			char const *old_literal_seq_end;
			size_t literal_seq_length;
			re_errno_t error;
do_literal:
			literal_seq_hasesc  = (unsigned char)*tokstart == '\\';
			literal_seq_isutf8  = RE_TOKEN_ISUTF8(tok);
			literal_seq_start   = tokstart;
			literal_seq_end     = self->rec_parser.rep_pos;
			old_literal_seq_end = self->rec_parser.rep_pos;
			literal_seq_length  = 1;
#ifndef __OPTIMIZE_SIZE__
			seq_followed_by_suffix = false;
#endif /* !__OPTIMIZE_SIZE__ */
			for (;;) {
				re_token_t lit = re_compiler_yield(self);
				if (!RE_TOKEN_ISLITERAL(lit)) {
					if (RE_TOKEN_ISSUFFIX(lit)) {
						/* literal  sequence immediately followed  by repetition token: "abcdef*"
						 * In  this case, we're only allowed to encode sequence "abcde", followed
						 * by a single-character sequence "f", since only said "f" is meant to be
						 * affected by the trailing "*"! */
						assert(old_literal_seq_end <= literal_seq_end);
						if (literal_seq_end != old_literal_seq_end) {
							assert(literal_seq_length > 1);
							--literal_seq_length;
#ifndef __OPTIMIZE_SIZE__
							seq_followed_by_suffix = true;
#endif /* !__OPTIMIZE_SIZE__ */
						} else {
							assert(literal_seq_length == 1);
						}
						literal_seq_end = old_literal_seq_end;
					}
					self->rec_parser.rep_pos = literal_seq_end;
					break;
				}
				literal_seq_hasesc |= (unsigned char)*literal_seq_end == '\\';
				literal_seq_isutf8 |= RE_TOKEN_ISUTF8(lit);
				old_literal_seq_end = literal_seq_end;
				literal_seq_end     = self->rec_parser.rep_pos;
				++literal_seq_length;
			}
			if (IF_NO_UTF8(self->rec_parser.rep_syntax))
				literal_seq_isutf8 = false; /* They're always just bytes! */

			/* Encode the literal sequence */
			alternation_prefix_dump();
			error = re_compiler_compile_literal_seq(self,
			                                        literal_seq_start,
			                                        literal_seq_end,
			                                        literal_seq_length,
			                                        literal_seq_hasesc,
			                                        literal_seq_isutf8);
			if unlikely(error != RE_NOERROR)
				return error;
#ifndef __OPTIMIZE_SIZE__
			if (seq_followed_by_suffix)
				goto again; /* Go ahead and compile the literal for the suffix that will follow */
#endif /* !__OPTIMIZE_SIZE__ */
		}
		break;

	}
done_prefix:
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
}


#define RE_EPSILON_JMP_ENCODE(baseop, offset) ((baseop) + 1 + (offset) - 3)
static_assert(REOP_GROUP_MATCH_JMIN == REOP_GROUP_MATCH_J3);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 3) == REOP_GROUP_MATCH_J3);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 4) == REOP_GROUP_MATCH_J4);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 5) == REOP_GROUP_MATCH_J5);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 6) == REOP_GROUP_MATCH_J6);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 7) == REOP_GROUP_MATCH_J7);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 8) == REOP_GROUP_MATCH_J8);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 9) == REOP_GROUP_MATCH_J9);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 10) == REOP_GROUP_MATCH_J10);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_MATCH, 11) == REOP_GROUP_MATCH_J11);
static_assert(REOP_GROUP_MATCH_JMAX == REOP_GROUP_MATCH_J11);
static_assert(REOP_GROUP_END_JMIN == REOP_GROUP_END_J3);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 3) == REOP_GROUP_END_J3);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 4) == REOP_GROUP_END_J4);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 5) == REOP_GROUP_END_J5);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 6) == REOP_GROUP_END_J6);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 7) == REOP_GROUP_END_J7);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 8) == REOP_GROUP_END_J8);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 9) == REOP_GROUP_END_J9);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 10) == REOP_GROUP_END_J10);
static_assert(RE_EPSILON_JMP_ENCODE(REOP_GROUP_END, 11) == REOP_GROUP_END_J11);
static_assert(REOP_GROUP_END_JMAX == REOP_GROUP_END_J11);


PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_compiler_set_group_epsilon_jmp)(uint8_t *p_group_instruction,
                                                  uint8_t num_bytes_skip_if_empty) {
	assert(num_bytes_skip_if_empty >= 3 && num_bytes_skip_if_empty <= 11);
	assertf(*p_group_instruction == REOP_GROUP_MATCH ||
	        *p_group_instruction == REOP_GROUP_END,
	        "*p_group_instruction = %#I8x",
	        *p_group_instruction);
	*p_group_instruction = RE_EPSILON_JMP_ENCODE(*p_group_instruction, num_bytes_skip_if_empty);
}


/* Compile a repeat expression */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_repeat)(struct re_compiler *__restrict self,
                                           uint8_t interval_min,
                                           uint8_t interval_max,
                                           bool interval_max_is_unbounded,
                                           bool expression_matches_epsilon) {
	size_t expr_size;

	/* Figure out the size of the affected expression */
	expr_size = (size_t)(self->rec_cpos - self->rec_estart);

	/* TODO: Any use-case that is implemented via `REOP_JMP_ONFAIL_DUMMY_AT' should
	 *       first  check if `expr_size'  is smaller than `EXPR_DUPLICATE_MAXSIZE'.
	 *       If that is the case, then encode the expression twice:
	 *       e.g. "X+" can be encoded as "XX*" (which doesn't use dummy on-fail handlers)
	 */

	/* When many are accepted, then `interval_max' is infinite */
	if (interval_max_is_unbounded) {
		if (interval_min == 0) {
			byte_t *writer, *label_1, *label_2;
			/* >> "X*"           REOP_JMP_ONFAIL 2f
			 * >>             1: <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `2f'
			 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 2f'
			 * >>                REOP_JMP_AND_RETURN_ONFAIL 1b
			 * >>             2: */
			if unlikely(!re_compiler_require(self, 9))
				goto err_nomem;

			/* Make space for code we want to insert in the front */
			writer = self->rec_estart;
			memmoveup(writer + 3, writer, expr_size);

			/* REOP_JMP_ONFAIL 2f */
			*writer++ = REOP_JMP_ONFAIL;
			label_2 = writer + 2 + expr_size + 3 + 3;
			delta16_set(writer, label_2 - (writer + 2));
			writer += 2;

			/* This is where the "1:" is in the pseudo-code */
			label_1 = writer;
			writer += expr_size;
			if (expression_matches_epsilon)
				re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 3);

			/* REOP_MAYBE_POP_ONFAIL */
			*writer++ = REOP_MAYBE_POP_ONFAIL;
			writer += 2;

			/* REOP_JMP_AND_RETURN_ONFAIL 1b */
			*writer++ = REOP_JMP_AND_RETURN_ONFAIL;
			delta16_set(writer, label_1 - (writer + 2));
			writer += 2;
			assert(label_2 == writer);

			self->rec_cpos = writer;
			goto done_suffix;
		} else if (interval_min == 1) {
			byte_t *writer, *label_1, *label_2;
			/* >> "X+"           REOP_JMP_ONFAIL_DUMMY_AT 2f
			 * >>             1: <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `2f'
			 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 2f'
			 * >>                REOP_JMP_AND_RETURN_ONFAIL 1b
			 * >>             2: */
			if unlikely(!re_compiler_require(self, 9))
				goto err_nomem;

			/* Make space for code we want to insert in the front */
			writer = self->rec_estart;
			memmoveup(writer + 3, writer, expr_size);

			/* REOP_JMP_ONFAIL_DUMMY_AT 2f */
			label_2 = writer + 3 + expr_size + 3 + 3;
			*writer++ = REOP_JMP_ONFAIL_DUMMY_AT;
			delta16_set(writer, label_2 - (writer + 2));
			writer += 2;

			/* <X> */
			label_1 = writer;
			writer += expr_size;
			if (expression_matches_epsilon)
				re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 3);

			/* REOP_MAYBE_POP_ONFAIL */
			*writer++ = REOP_MAYBE_POP_ONFAIL;
			writer += 2;

			/* REOP_JMP_AND_RETURN_ONFAIL 1b */
			*writer++ = REOP_JMP_AND_RETURN_ONFAIL;
			delta16_set(writer, label_1 - (writer + 2));
			writer += 2;
			assert(label_2 == writer);

			self->rec_cpos = writer;
			goto done_suffix;
		} else {
			uint8_t var_id;
			byte_t *writer, *label_1, *label_2, *label_3;
			/* >> "X{n,}"        REOP_SETVAR  {VAR = (n - 1)}
			 * >>            1:  REOP_JMP_ONFAIL_DUMMY_AT 3f
			 * >>            2:  <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `3f'
			 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 2f'
			 * >>                REOP_DEC_JMP {VAR}, 1b
			 * >>                REOP_JMP_AND_RETURN_ONFAIL 2b
			 * >>            3: */
			if unlikely(!re_compiler_require(self, 16))
				goto err_nomem;
			if unlikely(!re_compiler_allocvar(self, &var_id))
				goto err_esize;

			/* Make space for code we want to insert in the front */
			writer = self->rec_estart;
			memmoveup(writer + 6, writer, expr_size);

			/* REOP_SETVAR  {VAR = (n - 1)} */
			*writer++ = REOP_SETVAR;
			*writer++ = var_id;
			*writer++ = interval_min - 1;

			/* REOP_JMP_ONFAIL_DUMMY_AT 3f */
			label_1 = writer;
			label_3 = writer + 3 + expr_size + 3 + 4 + 3;
			*writer++ = REOP_JMP_ONFAIL_DUMMY_AT;
			delta16_set(writer, label_3 - (writer + 2));
			writer += 2;

			/* <X> */
			label_2 = writer;
			writer += expr_size;
			if (expression_matches_epsilon)
				re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 4 + 3);

			/* REOP_MAYBE_POP_ONFAIL */
			*writer++ = REOP_MAYBE_POP_ONFAIL;
			writer += 2;

			/* REOP_DEC_JMP {VAR}, 1b */
			*writer++ = REOP_DEC_JMP;
			*writer++ = var_id;
			delta16_set(writer, label_1 - (writer + 2));
			writer += 2;

			/* REOP_JMP_AND_RETURN_ONFAIL 2b */
			*writer++ = REOP_JMP_AND_RETURN_ONFAIL;
			delta16_set(writer, label_2 - (writer + 2));
			writer += 2;
			assert(label_3 == writer);

			self->rec_cpos = writer;
			goto done_suffix;
		}
		__builtin_unreachable();
	}

	if (interval_max == 0) {
		/* >> "X{0}"         REOP_NOP        // Or just no instruction at all
		 * -> Simply delete the current expression
		 *
		 * NOTE: If the current expression defined groups, then we have to
		 *       re-insert the `REOP_GROUP_START / REOP_GROUP_END' opcodes
		 *       so that those groups are properly matched by `regexec(3)' */
		byte_t *writer = self->rec_estart;
		byte_t *reader = self->rec_estart;
		for (; reader < self->rec_cpos; reader = libre_opcode_next(reader)) {
			byte_t opcode = *reader;
			if (opcode >= REOP_GROUP_END_JMIN && opcode <= REOP_GROUP_END_JMAX)
				opcode = REOP_GROUP_END; /* Undo Jn opcode transformation (groups are never followed by a loop suffix) */
			if (opcode == REOP_GROUP_START || opcode == REOP_GROUP_END) {
				uint8_t gid = reader[1];
				*writer++   = opcode; /* Group start/end instruction */
				*writer++   = gid;    /* Reference to group # */
				if (gid < lengthof(self->rec_grpinfo)) {
					/* The group can always be empty now */
					self->rec_grpinfo[gid] |= RE_COMPILER_GRPINFO_EPSILON;
				}
			}
		}
		self->rec_cpos = writer;
		goto done_suffix;
	}

	if (interval_min == 1) {
		uint8_t var_id;
		byte_t *writer, *label_1, *label_2;

		/* Check for special (and simple) case: always match exactly 1 */
		if (interval_max == 1)
			goto done_suffix;

		/* >> "X{1,m}"       REOP_SETVAR  {VAR = (m - 1)}
		 * >>                REOP_JMP_ONFAIL_DUMMY_AT 2f
		 * >>            1:  <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `2f'
		 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 2f'
		 * >>                REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR}, 1b
		 * >>            2: */
		if unlikely(!re_compiler_require(self, 13))
			goto err_nomem;
		if unlikely(!re_compiler_allocvar(self, &var_id))
			goto err_esize;

		/* Make space for code we want to insert in the front */
		writer = self->rec_estart;
		memmoveup(writer + 6, writer, expr_size);

		/* REOP_SETVAR  {VAR = (m - 1)} */
		*writer++ = REOP_SETVAR;
		*writer++ = var_id;
		*writer++ = interval_max - 1;

		/* REOP_JMP_ONFAIL_DUMMY_AT 3f */
		label_2 = writer + 3 + expr_size + 3 + 4;
		*writer++ = REOP_JMP_ONFAIL_DUMMY_AT;
		delta16_set(writer, label_2 - (writer + 2));
		writer += 2;

		/* <X> */
		label_1 = writer;
		writer += expr_size;
		if (expression_matches_epsilon)
			re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 4);

		/* REOP_MAYBE_POP_ONFAIL */
		*writer++ = REOP_MAYBE_POP_ONFAIL;
		writer += 2;

		/* REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR}, 1b */
		*writer++ = REOP_DEC_JMP_AND_RETURN_ONFAIL;
		*writer++ = var_id;
		delta16_set(writer, label_1 - (writer + 2));
		writer += 2;
		assert(label_2 == writer);

		self->rec_cpos = writer;
		goto done_suffix;
	}

	if (interval_min == 0) {
		if (interval_max == 1) {
			/* >> "X?"           REOP_JMP_ONFAIL 1f
			 * >>                <X>
			 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 1f'
			 * >>            1: */
			byte_t *writer;
			if unlikely(!re_compiler_require(self, 6))
				goto err_nomem;
			writer = self->rec_estart;
			memmoveup(writer + 3, writer, expr_size);

			/* REOP_JMP_ONFAIL 1f */
			*writer++ = REOP_JMP_ONFAIL;
			delta16_set(writer, expr_size + 3);
			writer += 2;

			/* <X> */
			writer += expr_size;

			/* REOP_MAYBE_POP_ONFAIL */
			*writer++ = REOP_MAYBE_POP_ONFAIL;
			writer += 2;

			self->rec_cpos = writer;
			goto done_suffix;
		} else {
			uint8_t var_id;
			byte_t *writer, *label_1, *label_2;
			/* >> "X{0,m}"       REOP_SETVAR  {VAR = (m - 1)}
			 * >>                REOP_JMP_ONFAIL 2f
			 * >>            1:  <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `2f'
			 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 2f'
			 * >>                REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR}, 1b
			 * >>            2: */
			if unlikely(!re_compiler_require(self, 13))
				goto err_nomem;
			if unlikely(!re_compiler_allocvar(self, &var_id))
				goto err_esize;

			/* Make space for code we want to insert in the front */
			writer = self->rec_estart;
			memmoveup(writer + 6, writer, expr_size);

			/* REOP_SETVAR  {VAR = (m - 1)} */
			*writer++ = REOP_SETVAR;
			*writer++ = var_id;
			*writer++ = interval_max - 1;

			/* REOP_JMP_ONFAIL 2f */
			label_2 = writer + 3 + expr_size + 3 + 4;
			*writer++ = REOP_JMP_ONFAIL;
			delta16_set(writer, label_2 - (writer + 2));
			writer += 2;

			/* <X> */
			label_1 = writer;
			writer += expr_size;
			if (expression_matches_epsilon)
				re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 4);

			/* REOP_MAYBE_POP_ONFAIL */
			*writer++ = REOP_MAYBE_POP_ONFAIL;
			writer += 2;

			/* REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR}, 1b */
			*writer++ = REOP_DEC_JMP_AND_RETURN_ONFAIL;
			*writer++ = var_id;
			delta16_set(writer, label_1 - (writer + 2));
			writer += 2;
			assert(label_2 == writer);

			self->rec_cpos = writer;
			goto done_suffix;
		}
		__builtin_unreachable();
	}

	if (interval_min == interval_max) {
		uint8_t var_id;
		byte_t *writer, *label_1;
		/* >> "X{n}"         REOP_SETVAR  {VAR = (n - 1)}
		 * >>             1: <X>
		 * >>                REOP_DEC_JMP {VAR}, 1b */
		if unlikely(!re_compiler_require(self, 7))
			goto err_nomem;
		if unlikely(!re_compiler_allocvar(self, &var_id))
			goto err_esize;

		/* Make space for code we want to insert in the front */
		writer = self->rec_estart;
		memmoveup(writer + 3, writer, expr_size);

		/* REOP_SETVAR  {VAR = (n - 1)} */
		*writer++ = REOP_SETVAR;
		*writer++ = var_id;
		*writer++ = interval_min - 1;

		/* This is where the "1:" is in the pseudo-code */
		label_1 = writer;
		writer += expr_size;

		/* REOP_DEC_JMP {VAR}, 1b */
		*writer++ = REOP_DEC_JMP;
		*writer++ = var_id;
		delta16_set(writer, label_1 - (writer + 2));
		writer += 2;

		self->rec_cpos = writer;
		goto done_suffix;
	}

	/* >> "X{n,m}"       REOP_SETVAR  {VAR1 = n - 1}
	 * >>                REOP_SETVAR  {VAR2 = (m - n)}
	 * >>            1:  REOP_JMP_ONFAIL_DUMMY_AT 3f
	 * >>            2:  <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `3f'
	 * >>                REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 3f'
	 * >>                REOP_DEC_JMP {VAR1}, 1b
	 * >>                REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR2}, 2b
	 * >>            3: */
	{
		uint8_t var1_id, var2_id;
		byte_t *writer, *label_1, *label_2, *label_3;
		if unlikely(!re_compiler_require(self, 20))
			goto err_nomem;

		/* Allocate variable IDs */
		if unlikely(!re_compiler_allocvar(self, &var1_id))
			goto err_esize;
		if unlikely(!re_compiler_allocvar(self, &var2_id))
			goto err_esize;

		/* Make space for code we want to insert in the front */
		writer = self->rec_estart;
		memmoveup(writer + 9, writer, expr_size);

		/* REOP_SETVAR  {VAR1 = n - 1} */
		*writer++ = REOP_SETVAR;
		*writer++ = var1_id;
		*writer++ = interval_min - 1;

		/* REOP_SETVAR  {VAR2 = (m - n)} */
		*writer++ = REOP_SETVAR;
		*writer++ = var2_id;
		*writer++ = interval_max - interval_min;

		/* 1:  REOP_JMP_ONFAIL_DUMMY_AT 3f */
		label_1 = writer;
		label_3 = writer + 3 + expr_size + 3 + 4 + 4;
		*writer++ = REOP_JMP_ONFAIL_DUMMY_AT;
		delta16_set(writer, label_3 - (writer + 2));
		writer += 2;

		/* 2:  <X>     // Last instruction is `REOP_*_Jn(N)'-transformed to jump to `3f' */
		label_2 = writer;
		writer += expr_size;
		if (expression_matches_epsilon)
			re_compiler_set_group_epsilon_jmp(writer - 2, 3 + 4 + 4);

		/* REOP_MAYBE_POP_ONFAIL */
		*writer++ = REOP_MAYBE_POP_ONFAIL;
		writer += 2;

		/* REOP_DEC_JMP {VAR1}, 1b */
		*writer++ = REOP_DEC_JMP;
		*writer++ = var1_id;
		delta16_set(writer, label_1 - (writer + 2));
		writer += 2;

		/* REOP_DEC_JMP_AND_RETURN_ONFAIL {VAR2}, 2b */
		*writer++ = REOP_DEC_JMP_AND_RETURN_ONFAIL;
		*writer++ = var2_id;
		delta16_set(writer, label_2 - (writer + 2));
		writer += 2;
		assert(label_3 == writer);

		self->rec_cpos = writer;
	}

done_suffix:
	return RE_NOERROR;
err_nomem:
	return RE_ESPACE;
err_esize:
	return RE_ESIZE;
}

/* Compile prefix expressions: literals, '[...]' and '(...)'
 * @param: prefix_status: Either `RE_NOERROR', or `RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP'
 */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_suffix)(struct re_compiler *__restrict self,
                                           re_errno_t prefix_status) {
	re_token_t tok;
	char const *tokstart;
	assertf(self->rec_estart <= self->rec_cpos,
	        "self->rec_estart: %p\n"
	        "self->rec_cpos:   %p",
	        self->rec_estart,
	        self->rec_cpos);

	/* Start parsing the current expression */
	tokstart = self->rec_parser.rep_pos;
	tok      = re_compiler_yield(self);
	if (RE_TOKEN_ISSUFFIX(tok)) {
		re_errno_t error;
		uint8_t interval_min = 1, interval_max = 1;
		bool accept_zero = false; /* true if `0'   is accepted (encountered '*' or '?') */
		bool accept_many = false; /* true if `> 1' is accepted (encountered '*' or '+') */
		for (;;) {
			if (tok == RE_TOKEN_STARTINTERVAL) {
				/* Parse an interval */
				struct re_interval interval;
				if unlikely(!parse_interval((char const **)&self->rec_parser.rep_pos,
				                            self->rec_parser.rep_syntax, &interval))
					goto err_badinterval;
				accept_many |= interval.ri_many;

				/* Merge new interval with already-parsed interval */
				if unlikely(!accept_zero && OVERFLOW_UMUL(interval_min, interval.ri_min, &interval_min))
					goto err_badinterval;
				if unlikely(!accept_many && OVERFLOW_UMUL(interval_max, interval.ri_max, &interval_max))
					goto err_badinterval;
			} else {
				accept_zero |= tok == RE_TOKEN_STAR || tok == RE_TOKEN_QMARK;
				accept_many |= tok == RE_TOKEN_STAR || tok == RE_TOKEN_PLUS;
			}
			tokstart = self->rec_parser.rep_pos;
			tok      = re_compiler_yield(self);
			if (!RE_TOKEN_ISSUFFIX(tok)) {
				self->rec_parser.rep_pos = tokstart;
				break;
			}
		}
		/* When zero is accepted, then the interval always starts at 0 */
		if (accept_zero)
			interval_min = 0;

		/* Encode the interval */
		error = re_compiler_compile_repeat(self, interval_min, interval_max, accept_many,
		                                   prefix_status == RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP);
		if unlikely(error != RE_NOERROR)
			return error;
	} else {
		self->rec_parser.rep_pos = tokstart;
	}
/*done_suffix:*/
	/* Following a suffix, the expression start becomes invalid */
	DBG_memset(&self->rec_estart, 0xcc, sizeof(self->rec_estart));
	return RE_NOERROR;
err_badinterval:
	return RE_BADBR;
}


PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_compiler_thread_fwd_jump)(byte_t *__restrict p_jmp_instruction) {
	byte_t *target_instruction;
	int16_t delta;
	assert(p_jmp_instruction[0] == REOP_JMP);
	delta = delta16_get(&p_jmp_instruction[1]);
	assertf(delta >= 0, "delta: %I16d", delta);
	target_instruction = p_jmp_instruction + 3 + delta;
	while (*target_instruction == REOP_NOP)
		++target_instruction;

	/* Recursively thread target jumps. */
	if (target_instruction[0] == REOP_JMP) {
		int16_t target_delta;
		int32_t total_delta;
		re_compiler_thread_fwd_jump(target_instruction);
		target_delta = delta16_get(&target_instruction[1]);
		assert(target_delta >= 0);
		total_delta = delta + 3 + target_delta;
		if (total_delta <= INT16_MAX) {
			/* Able to thread this jump! */
			delta16_set(&p_jmp_instruction[1], total_delta);
		}
	}
}


/* Compile a sequence of prefix/suffix expressions, as well as '|'
 * @param: alternation_prefix: A code-blob that is inserted before
 *                             the  body  of  every   alternation.
 * @param: alternation_prefix_size: *ditto* */
#if ALTERNATION_PREFIX_MAXLEN > 0
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC re_compiler_compile_alternation)(struct re_compiler *__restrict self,
                                                void const *alternation_prefix,
                                                size_t alternation_prefix_size)
#else /* ALTERNATION_PREFIX_MAXLEN > 0 */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC _re_compiler_compile_alternation)(struct re_compiler *__restrict self)
#endif /* ALTERNATION_PREFIX_MAXLEN <= 0 */
{
	re_errno_t error;
	size_t initial_alternation_jmpoff;
	size_t previous_alternation_deltaoff;
	size_t current_alternation_startoff;
	re_token_t tok;
	char const *tokstart;
	initial_alternation_jmpoff    = (size_t)-1;
	previous_alternation_deltaoff = (size_t)-1;

again:
	current_alternation_startoff = (size_t)(self->rec_cpos - self->rec_cbase);

	/* Insert the alternation prefix (if one was given) */
#if ALTERNATION_PREFIX_MAXLEN > 0
	if (alternation_prefix_size > 0) {
		if (!re_compiler_putn(self, alternation_prefix, alternation_prefix_size))
			goto err_nomem;
	}
#endif /* ALTERNATION_PREFIX_MAXLEN > 0 */

	/* Compile expression sequences. */
	for (;;) {
		char const *old_tokptr;
		old_tokptr = self->rec_parser.rep_pos;

		/* Parse an expression */
		error = re_compiler_compile_prefix(self);
		if unlikely(error != RE_NOERROR &&
		            error != RE_COMPILER_COMPILE_PREFIX__AFTER_EPSILON_GROUP)
			goto err;
		error = re_compiler_compile_suffix(self, error);
		if unlikely(error != RE_NOERROR)
			goto err;

		/* Check if we're moving forward in the pattern */
		assert(self->rec_parser.rep_pos >= old_tokptr);
		if (self->rec_parser.rep_pos == old_tokptr)
			break; /* Unchanged parser position -> we're at a token that cannot be processed. */
	}

	/* Check what's the next token */
	tokstart = self->rec_parser.rep_pos;
	tok      = re_compiler_yield(self);
	if (tok != RE_TOKEN_ALTERNATION) {
		/* Rewind to start of token */
		self->rec_parser.rep_pos = tokstart;
		/* Fill in the delta of the `REOP_JMP ...' from a preceding alternation. */
		if (previous_alternation_deltaoff != (size_t)-1) {
			byte_t *previous_alternation_deltaptr;
			int16_t previous_alternation_skipdelta;
			previous_alternation_deltaptr  = self->rec_cbase + previous_alternation_deltaoff;
			previous_alternation_skipdelta = (int16_t)(self->rec_cpos - (previous_alternation_deltaptr + 2));
			assert(previous_alternation_skipdelta >= 0);
			delta16_set(previous_alternation_deltaptr, previous_alternation_skipdelta);
		}
	} else {
		byte_t *current_alternation_startptr;
		size_t current_alternation_size;

		/* Ensure that we've got enough memory for `REOP_JMP_ONFAIL', `REOP_MAYBE_POP_ONFAIL', and `REOP_JMP' */
		if unlikely(!re_compiler_require(self, 9))
			goto err_nomem;
		current_alternation_startptr = self->rec_cbase + current_alternation_startoff;
		current_alternation_size     = (size_t)(self->rec_cpos - current_alternation_startptr);
		memmoveup(current_alternation_startptr + 3,
		          current_alternation_startptr,
		          current_alternation_size);

		/* Insert the leading `REOP_JMP_ONFAIL' that points to the next alternation */
		*current_alternation_startptr++ = REOP_JMP_ONFAIL;
		delta16_set(current_alternation_startptr, current_alternation_size + 6);
		current_alternation_startptr += 2;
		current_alternation_startptr += current_alternation_size;
		*current_alternation_startptr++ = REOP_MAYBE_POP_ONFAIL;
		current_alternation_startptr += 2;

		/* Write  the `REOP_JMP' instruction after the alternation
		 * Note that the offset of this instruction will be filled
		 * after the next alternation has been compiled. */
		if (initial_alternation_jmpoff == (size_t)-1) {
			/* Remember the first jump location so we can jump-thread-optimize it later! */
			initial_alternation_jmpoff = (size_t)(current_alternation_startptr - self->rec_cbase);
		}

		/* Fill in the delta of the `REOP_JMP ...' from a preceding alternation. */
		if (previous_alternation_deltaoff != (size_t)-1) {
			byte_t *previous_alternation_deltaptr;
			int16_t previous_alternation_skipdelta;
			previous_alternation_deltaptr  = self->rec_cbase + previous_alternation_deltaoff;
			previous_alternation_skipdelta = (int16_t)(current_alternation_startptr - (previous_alternation_deltaptr + 2));
			assert(previous_alternation_skipdelta >= 0);
			delta16_set(previous_alternation_deltaptr, previous_alternation_skipdelta);
		}

		*current_alternation_startptr++ = REOP_JMP;
		previous_alternation_deltaoff = (size_t)(current_alternation_startptr - self->rec_cbase);
		DBG_memset(current_alternation_startptr, 0xcc, 2); /* UNDEFINED! (filled later) */
		current_alternation_startptr += 2;
		self->rec_cpos = current_alternation_startptr;
		goto again;
	}

	if (initial_alternation_jmpoff != (size_t)-1) {
		byte_t *initial_alternation_jmp;
		/* Up until now, we've compiled the expression "X|Y|Z" as:
		 * >>    REOP_JMP_ONFAIL  1f
		 * >>    <X>
		 * >>    REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 1f'
		 * >>    REOP_JMP         2f // << `initial_alternation_jmp' points to this instruction
		 * >> 1: REOP_JMP_ONFAIL  3f
		 * >>    <Y>
		 * >>    REOP_MAYBE_POP_ONFAIL   // Replaced with `REOP_POP_ONFAIL_AT 1f'
		 * >> 2: REOP_JMP         4f
		 * >> 3: <Z>
		 * >> 4:
		 *
		 * But  now we want to optimize this by (trying to) thread these `REOP_JMP' into
		 * each  other, such that rather than jumping from one to the next, all jumps go
		 * as far forward in the expression as possible, (or at least as far as possible
		 * without causing a signed int16 overflow) */

		/* Make sure that the end of the jump-chain isn't out-of-bounds */
		if unlikely(!re_compiler_require(self, 1))
			goto err_nomem;
		/* Anything  that isn't `REOP_JMP'  or `REOP_NOP' works here;
		 * (needed as end-marker for `re_compiler_thread_fwd_jump()') */
		*self->rec_cpos = REOP_MATCHED;

		initial_alternation_jmp = self->rec_cbase + initial_alternation_jmpoff;
		re_compiler_thread_fwd_jump(initial_alternation_jmp);
	}

	return RE_NOERROR;
err:
	return error;
err_nomem:
	return RE_ESPACE;
}


/* Parse  and compile the pattern given to `self' to generate code.
 * Even  upon error, `self'  remains in a  valid state (except that
 * you're  not allowed to call `re_compiler_compile(3R)' again), so
 * that the caller has to invoke `re_compiler_fini(3R)' in order to
 * perform cleanup.
 * Upon success, members of `self' are initialized as:
 * - *rec_parser.rep_pos    == '\0'
 * - rec_parser.rep_pos     == rec_parser.rep_end
 * - rec_parser.rep_syntax  == <unchanged>
 * - rec_parser.rec_cbase   == <pointer-to-struct re_code>
 * - rec_parser.rec_estart  == <undefined>
 * - rec_parser.rec_cpos    == <undefined>
 * - rec_parser.rec_cend    == <code-end-pointer (1 past the `REOP_MATCHED[_PERFECT]' opcode)>
 * - rec_parser.rec_ngrp    == <greatest-referenced-group + 1>
 * - rec_parser.rec_nvar    == <greatest-referenced-var + 1>
 * - rec_parser.rec_grpinfo == <undefined>
 * @return: RE_NOERROR:  Success
 * @return: RE_BADPAT:   General pattern syntax error.
 * @return: RE_ECOLLATE: Unsupported/unknown collating character (in '[[.xxx.]]' and '[[=xxx=]]')
 * @return: RE_ECTYPE:   Invalid/unknown character class name.
 * @return: RE_EESCAPE:  Trailing backslash.
 * @return: RE_ESUBREG:  Invalid back reference.
 * @return: RE_EBRACK:   Unmatched '['.
 * @return: RE_EPAREN:   Unmatched '('.
 * @return: RE_EBRACE:   Unmatched '{'.
 * @return: RE_BADBR:    Invalid contents of '{...}'.
 * @return: RE_ERANGE:   Invalid range end (e.g. '[z-a]').
 * @return: RE_ESPACE:   Out of memory.
 * @return: RE_BADRPT:   Nothing is preceding '+', '*', '?' or '{'.
 * @return: RE_EEND:     Unexpected end of pattern.
 * @return: RE_ESIZE:    Compiled pattern bigger than 2^16 bytes.
 * @return: RE_ERPAREN:  Unmatched ')' (only when `RE_SYNTAX_UNMATCHED_RIGHT_PAREN_ORD' wasn't set)
 * @return: RE_EILLSEQ:  Illegal unicode character (when `RE_NO_UTF8' wasn't set)
 * @return: RE_EILLSET:  Tried to combine raw bytes with unicode characters in charsets (e.g. "[Ä\xC3]") */
INTERN WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC libre_compiler_compile)(struct re_compiler *__restrict self) {
	re_errno_t error;
	uint8_t finish_opcode;
	re_token_t trailing_token;

	/* Make space for the `struct re_code' header */
	if unlikely(!re_compiler_require(self, offsetof(struct re_code, rc_code)))
		goto err_nomem;
	self->rec_cpos += offsetof(struct re_code, rc_code);

	/* Initialize the regex code header */
	self->rec_code->rc_ngrps = 0;
	self->rec_code->rc_nvars = 0;
	self->rec_code->rc_flags = RE_CODE_FLAG_NORMAL;
	self->rec_code->rc_flags |= RE_CODE_FLAG_OPTGROUPS; /* TODO: Only set this flag if necessary. */

	/* Do the actual compilation */
	error = re_compiler_compile_alternation(self, NULL, 0);
	if unlikely(error != RE_NOERROR)
		goto err;

	/* Check that everything has been parsed. */
	trailing_token = re_compiler_yield(self);
	if unlikely(trailing_token != RE_TOKEN_EOF) {
		if (trailing_token == RE_TOKEN_ENDGROUP)
			return RE_ERPAREN;
		return RE_BADPAT;
	}

	/* Terminate the expression with a `REOP_MATCHED' opcode */
	finish_opcode = REOP_MATCHED;
	if (IF_NO_POSIX_BACKTRACKING(self->rec_parser.rep_syntax))
		finish_opcode = REOP_MATCHED_PERFECT;
	if (!re_compiler_putc(self, finish_opcode))
		goto err_nomem;

	/* Apply peephole optimizations to the code produced by `self' */
	libre_compiler_peephole(self);

	/* (try to) free unused memory from the code-buffer. */
	if likely(self->rec_cpos < self->rec_cend) {
		byte_t *newbase;
		size_t reqsize;
		reqsize = (size_t)(self->rec_cpos - self->rec_cbase);
		newbase = (byte_t *)realloc(self->rec_cbase, reqsize);
		if likely(newbase) {
			self->rec_cbase = newbase;
			self->rec_cend  = newbase + reqsize;
		}
	}
	DBG_memset(&self->rec_cpos, 0xcc, sizeof(self->rec_cpos));
	DBG_memset(&self->rec_estart, 0xcc, sizeof(self->rec_estart));

	/* Generate the fast-map, as well as the min-match attribute. */
	libre_code_makefast(self->rec_code);

	return RE_NOERROR;
err_nomem:
	error = RE_ESPACE;
err:
	if (error == RE_ESPACE) {
		if ((size_t)(self->rec_cend - self->rec_cbase) >= RE_COMP_MAXSIZE)
			error = RE_ESIZE;
	}
	return error;
}



#if !defined(LIBREGEX_NO_RE_CODE_DISASM) && !defined(NDEBUG)
/* Print a disassembly of `self' (for debugging) */
INTERN NONNULL((1)) ssize_t
NOTHROW_NCX(CC libre_code_disasm)(struct re_code const *__restrict self,
                                  pformatprinter printer, void *arg) {
#define DO(x)                         \
	do {                              \
		if unlikely((temp = (x)) < 0) \
			goto err;                 \
		result += temp;               \
	}	__WHILE0
#define PRINT(x)    DO((*printer)(arg, x, COMPILER_STRLEN(x)))
#define printf(...) DO(format_printf(printer, arg, __VA_ARGS__))
	byte_t const *pc, *nextpc;
	ssize_t temp, result = 0;
	unsigned int i;
	bool is_first_fmap_entry;
	PRINT("fmap: [");
	/* Print the fast-map */
	is_first_fmap_entry = true;
	for (i = 0; i < 256;) {
		char buf[3];
		unsigned int fend, fcnt;
		byte_t fmap_offset;
		fmap_offset = self->rc_fmap[i];
		if (fmap_offset == 0xff) {
			++i;
			continue;
		}
		fend = i + 1;
		while (fend < 256 && self->rc_fmap[fend] == fmap_offset)
			++fend;
		fcnt = fend - i;
		if (fcnt > 3) {
			buf[0] = (char)i;
			buf[1] = '-';
			buf[2] = (char)(fend - 1);
			fcnt   = 3;
		} else {
			buf[0] = (char)(i + 0);
			buf[1] = (char)(i + 1);
			buf[2] = (char)(i + 2);
		}
		printf("%s\t%$q: %#.4" PRIx8 "\n", is_first_fmap_entry ? "\n" : "",
		       (size_t)fcnt, buf, (uint8_t)fmap_offset);
		is_first_fmap_entry = false;
		i = fend;
	}
	printf("]\n"
	       "minmatch: %" PRIuSIZ "\n"
	       "ngrps: %" PRIu16 "\n"
	       "nvars: %" PRIu16 "\n",
	       self->rc_minmatch,
	       self->rc_ngrps,
	       self->rc_nvars);
	for (pc = self->rc_code;; pc = nextpc) {
		size_t offset;
		byte_t opcode;
		char const *opcode_repr;
		nextpc = libre_opcode_next(pc);
		offset = (size_t)(pc - self->rc_code);
		opcode = *pc++;
		printf("%#.4" PRIxSIZ ": ", offset);
		switch (opcode) {

		case REOP_EXACT: {
			uint8_t len = *pc++;
			printf("exact %$q", (size_t)len, pc);
		}	break;

		case REOP_EXACT_ASCII_ICASE: {
			uint8_t len = *pc++;
			printf("exact_ascii_icase %$q", (size_t)len, pc);
		}	break;

		case REOP_EXACT_UTF8_ICASE: {
			++pc;
			printf("exact_ascii_icase %$q", (size_t)(nextpc - pc), pc);
		}	break;

		case REOP_BYTE:
		case REOP_NBYTE: {
			printf("%sbyte %$q",
			       opcode == REOP_NBYTE ? "n" : "",
			       (size_t)1, pc);
		}	break;

		case REOP_BYTE2:
		case REOP_NBYTE2: {
			printf("%sbyte2 %$q",
			       opcode == REOP_NBYTE2 ? "n" : "",
			       (size_t)2, pc);
		}	break;

		case REOP_RANGE:
		case REOP_NRANGE: {
			printf("%srange '%#$q-%#$q'",
			       opcode == REOP_NRANGE ? "n" : "",
			       (size_t)1, pc + 0,
			       (size_t)1, pc + 1);
		}	break;

		case REOP_CONTAINS_UTF8:
		case REOP_NCONTAINS_UTF8: {
			++pc;
			printf("%scontains_utf8 %$q",
			       opcode == REOP_NCONTAINS_UTF8 ? "n" : "",
			       (size_t)(nextpc - pc), pc);
		}	break;

		case REOP_CS_BYTE:
		case REOP_CS_UTF8:
		case REOP_NCS_UTF8: {
			uint8_t cs_opcode;
			bool isfirst;
			switch (opcode) {
			case REOP_CS_BYTE:
				opcode_repr = "cs_byte";
				break;
			case REOP_CS_UTF8:
				opcode_repr = "cs_utf8";
				break;
			case REOP_NCS_UTF8:
				opcode_repr = "ncs_utf8";
				break;
			default: __builtin_unreachable();
			}
			printf("%s [", opcode_repr);
			isfirst = true;
			while ((cs_opcode = *pc++) != RECS_DONE) {
				if (!isfirst)
					PRINT(", ");
				isfirst = false;
				switch (cs_opcode) {

				case RECS_CHAR:
				case RECS_CHAR2: {
					byte_t const *endpc = pc;
					printf("char%s ", cs_opcode == RECS_CHAR2 ? "2" : "");
					if (opcode == REOP_CS_BYTE) {
						endpc += 1;
						if (cs_opcode == RECS_CHAR2)
							endpc += 1;
					} else {
						endpc += unicode_utf8seqlen[*endpc];
						if (cs_opcode == RECS_CHAR2)
							endpc += unicode_utf8seqlen[*endpc];
					}
					printf("%$q", (size_t)(endpc - pc), pc);
					pc = endpc;
				}	break;

				case RECS_RANGE:
				case RECS_RANGE_ICASE:
					printf("range%s ", cs_opcode == RECS_RANGE_ICASE ? "_icase" : "");
					if (opcode == REOP_CS_BYTE) {
						printf("'%#$q-%#$q'", (size_t)1, &pc[0], (size_t)1, &pc[1]);
						pc += 2;
					} else {
						byte_t const *char2;
						byte_t const *endp;
						char2 = pc + unicode_utf8seqlen[*pc];
						endp  = char2 + unicode_utf8seqlen[*char2];
						printf("'%#$q-%#$q'", (size_t)(char2 - pc), pc, (size_t)(endp - char2), char2);
						pc = endp;
					}
					break;

				case RECS_CONTAINS: {
					uint8_t count = *pc++;
					byte_t const *cs_opcode_end;
					assert(count >= 1);
					PRINT("contains ");
					if (opcode == REOP_CS_BYTE) {
						cs_opcode_end = pc + count;
					} else {
						cs_opcode_end = pc;
						do {
							cs_opcode_end += unicode_utf8seqlen[*cs_opcode_end];
						} while (--count);
					}
					printf("%$q", (size_t)(cs_opcode_end - pc), pc);
					pc = cs_opcode_end;
				}	break;

				case_RECS_ISX_MIN_to_MAX: {
					char const *cs_name;
					if (opcode == REOP_CS_BYTE)
						goto do_cs_bitset;
					switch (cs_opcode) {
					case RECS_ISCNTRL:
						cs_name = "cntrl";
						break;
					case RECS_ISSPACE:
						cs_name = "space";
						break;
					case RECS_ISUPPER:
						cs_name = "upper";
						break;
					case RECS_ISLOWER:
						cs_name = "lower";
						break;
					case RECS_ISALPHA:
						cs_name = "alpha";
						break;
					case RECS_ISDIGIT:
						cs_name = "digit";
						break;
					case RECS_ISXDIGIT:
						cs_name = "xdigit";
						break;
					case RECS_ISALNUM:
						cs_name = "alnum";
						break;
					case RECS_ISPUNCT:
						cs_name = "punct";
						break;
					case RECS_ISGRAPH:
						cs_name = "graph";
						break;
					case RECS_ISPRINT:
						cs_name = "print";
						break;
					case RECS_ISBLANK:
						cs_name = "blank";
						break;
					case RECS_ISSYMSTRT:
						cs_name = "symstrt";
						break;
					case RECS_ISSYMCONT:
						cs_name = "symcont";
						break;
					case RECS_ISTAB:
						cs_name = "tab";
						break;
					case RECS_ISWHITE:
						cs_name = "white";
						break;
					case RECS_ISEMPTY:
						cs_name = "empty";
						break;
					case RECS_ISLF:
						cs_name = "lf";
						break;
					case RECS_ISHEX:
						cs_name = "hex";
						break;
					case RECS_ISTITLE:
						cs_name = "title";
						break;
					case RECS_ISNUMERIC:
						cs_name = "numeric";
						break;
					default: __builtin_unreachable();
					}
					printf("is%s", cs_name);
				}	break;

				default:
					if (cs_opcode >= RECS_BITSET_MIN &&
					    cs_opcode <= (opcode == REOP_CS_BYTE ? RECS_BITSET_MAX_BYTE
					                                         : RECS_BITSET_MAX_UTF8)) {
						uint8_t minch, bitset_size;
						unsigned int bitset_bits;
do_cs_bitset:
						minch       = RECS_BITSET_GETBASE(cs_opcode);
						bitset_size = RECS_BITSET_GETBYTES(cs_opcode);
						bitset_bits = bitset_size * 8;
						PRINT("bitset [");
						for (i = 0; i < bitset_bits;) {
							char repr[3];
							unsigned int endi, rangec;
							if ((pc[i / 8] & (1 << (i % 8))) == 0) {
								++i;
								continue;
							}
							endi = i + 1;
							while (endi < bitset_bits && (pc[endi / 8] & (1 << (endi % 8))) != 0)
								++endi;
							rangec = endi - i;
							if (rangec > 3) {
								repr[0] = (char)(minch + i);
								repr[1] = '-';
								repr[2] = (char)(minch + endi - 1);
								rangec  = 3;
							} else {
								repr[0] = (char)(minch + i + 0);
								repr[1] = (char)(minch + i + 1);
								repr[2] = (char)(minch + i + 2);
							}
							printf("%#$q", (size_t)rangec, repr);
							i = endi;
						}
						pc += bitset_size;
						PRINT("]");
						break;
					}
					printf("<BAD BYTE %#.2" PRIx8 ">", cs_opcode);
					break;
				}
			}
			PRINT("]");
			assertf(pc == nextpc, "pc - nextpc = %Id", pc - nextpc);
		}	break;

		case REOP_GROUP_MATCH: {
			uint8_t gid = *pc++;
			printf("group_match %" PRIu8, gid);
		}	break;

		case_REOP_GROUP_MATCH_JMIN_to_JMAX: {
			uint8_t gid       = *pc++;
			byte_t const *jmp = pc + REOP_GROUP_MATCH_Joff(opcode);
			printf("group_match %" PRIu8 ", @%#.4" PRIxSIZ,
			       gid, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_GROUP_START: {
			uint8_t gid = *pc++;
			printf("group_start %" PRIu8, gid);
		}	break;

		case REOP_GROUP_END: {
			uint8_t gid = *pc++;
			printf("group_end %" PRIu8, gid);
		}	break;

		case_REOP_GROUP_END_JMIN_to_JMAX: {
			uint8_t gid       = *pc++;
			byte_t const *jmp = pc + REOP_GROUP_END_Joff(opcode);
			printf("group_end %" PRIu8 ", @%#.4" PRIxSIZ,
			       gid, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_POP_ONFAIL_AT: {
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("pop_onfail_at @%#.4" PRIxSIZ, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_JMP_ONFAIL: {
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("jmp_onfail @%#.4" PRIxSIZ, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_JMP_ONFAIL_DUMMY_AT: {
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("jmp_onfail_dummy_at @%#.4" PRIxSIZ, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_JMP: {
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("jmp @%#.4" PRIxSIZ, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_JMP_AND_RETURN_ONFAIL: {
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("jmp_and_return_onfail @%#.4" PRIxSIZ, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_DEC_JMP: {
			uint8_t varid     = *pc++;
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("dec_jmp %" PRIu8 ", @%#.4" PRIxSIZ, varid, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_DEC_JMP_AND_RETURN_ONFAIL: {
			uint8_t varid     = *pc++;
			byte_t const *jmp = pc + 2 + delta16_get(pc);
			printf("dec_jmp_and_return_onfail %" PRIu8 ", @%#.4" PRIxSIZ, varid, (size_t)(jmp - self->rc_code));
		}	break;

		case REOP_SETVAR: {
			uint8_t varid = *pc++;
			uint8_t value = *pc++;
			printf("setvar %" PRIu8 ", %" PRIu8, varid, value);
		}	break;

#define SIMPLE_OPCODE(opcode, repr) \
		case opcode:                \
			opcode_repr = repr;     \
			goto do_print_opcode_repr
#ifdef REOP_ANY
		SIMPLE_OPCODE(REOP_ANY, "any");
#endif /* REOP_ANY */
#ifdef REOP_ANY_UTF8
		SIMPLE_OPCODE(REOP_ANY_UTF8, "any");
#endif /* REOP_ANY_UTF8 */
#ifdef REOP_ANY_NOTLF
		SIMPLE_OPCODE(REOP_ANY_NOTLF, "any_notlf");
#endif /* REOP_ANY_NOTLF */
#ifdef REOP_ANY_NOTLF_UTF8
		SIMPLE_OPCODE(REOP_ANY_NOTLF_UTF8, "any_notlf");
#endif /* REOP_ANY_NOTLF_UTF8 */
#ifdef REOP_ANY_NOTNUL
		SIMPLE_OPCODE(REOP_ANY_NOTNUL, "any_notnul");
#endif /* REOP_ANY_NOTNUL */
#ifdef REOP_ANY_NOTNUL_UTF8
		SIMPLE_OPCODE(REOP_ANY_NOTNUL_UTF8, "any_notnul");
#endif /* REOP_ANY_NOTNUL_UTF8 */
#ifdef REOP_ANY_NOTNUL_NOTLF
		SIMPLE_OPCODE(REOP_ANY_NOTNUL_NOTLF, "any_notnul_notlf");
#endif /* REOP_ANY_NOTNUL_NOTLF */
#ifdef REOP_ANY_NOTNUL_NOTLF_UTF8
		SIMPLE_OPCODE(REOP_ANY_NOTNUL_NOTLF_UTF8, "any_notnul_notlf_utf8");
#endif /* REOP_ANY_NOTNUL_NOTLF_UTF8 */
		SIMPLE_OPCODE(REOP_AT_SOI, "at_soi");
		SIMPLE_OPCODE(REOP_AT_EOI, "at_eoi");
		SIMPLE_OPCODE(REOP_AT_SOL, "at_sol");
		SIMPLE_OPCODE(REOP_AT_SOL_UTF8, "at_sol_utf8");
		SIMPLE_OPCODE(REOP_AT_EOL, "at_eol");
		SIMPLE_OPCODE(REOP_AT_EOL_UTF8, "at_eol_utf8");
		SIMPLE_OPCODE(REOP_AT_SOXL, "at_soxl");
		SIMPLE_OPCODE(REOP_AT_SOXL_UTF8, "at_soxl_utf8");
		SIMPLE_OPCODE(REOP_AT_EOXL, "at_eoxl");
		SIMPLE_OPCODE(REOP_AT_EOXL_UTF8, "at_eoxl_utf8");
		SIMPLE_OPCODE(REOP_AT_WOB, "at_wob");
		SIMPLE_OPCODE(REOP_AT_WOB_UTF8, "at_wob_utf8");
		SIMPLE_OPCODE(REOP_AT_WOB_NOT, "at_wob_not");
		SIMPLE_OPCODE(REOP_AT_WOB_UTF8_NOT, "at_wob_utf8_not");
		SIMPLE_OPCODE(REOP_AT_SOW, "at_sow");
		SIMPLE_OPCODE(REOP_AT_SOW_UTF8, "at_sow_utf8");
		SIMPLE_OPCODE(REOP_AT_EOW, "at_eow");
		SIMPLE_OPCODE(REOP_AT_EOW_UTF8, "at_eow_utf8");
		SIMPLE_OPCODE(REOP_AT_SOS_UTF8, "at_sos_utf8");
		SIMPLE_OPCODE(REOP_NOP, "nop");
		SIMPLE_OPCODE(REOP_JMP_ONFAIL_DUMMY, "jmp_onfail_dummy");
		SIMPLE_OPCODE(REOP_POP_ONFAIL, "pop_onfail");
		SIMPLE_OPCODE(REOP_MATCHED, "matched");
		SIMPLE_OPCODE(REOP_MAYBE_POP_ONFAIL, "[maybe_pop_onfail]");
#undef SIMPLE_OPCODE
		case REOP_MATCHED_PERFECT:
			opcode_repr = "matched_perfect";
do_print_opcode_repr:
			DO((*printer)(arg, opcode_repr, strlen(opcode_repr)));
			break;

		default:
			printf(".byte %#" PRIx8, opcode);
			for (; pc < nextpc; ++pc)
				printf(", %#" PRIx8, *pc);
			break;
		}
		PRINT("\n");
		if (opcode == REOP_MATCHED ||
		    opcode == REOP_MATCHED_PERFECT)
			break;
	}

	return result;
err:
	return temp;
#undef printf
#undef PRINT
#undef DO
}
#endif /* !LIBREGEX_NO_RE_CODE_DISASM && !NDEBUG */



#undef tswap
#undef delta16_set
#undef delta16_get
#undef DBG_memset
#undef re_parser_yield

DEFINE_PUBLIC_ALIAS(re_parser_yield, libre_parser_yield);
DEFINE_PUBLIC_ALIAS(re_compiler_compile, libre_compiler_compile);
#if !defined(LIBREGEX_NO_RE_CODE_DISASM) && !defined(NDEBUG)
DEFINE_PUBLIC_ALIAS(re_code_disasm, libre_code_disasm);
#endif /* !LIBREGEX_NO_RE_CODE_DISASM && !NDEBUG */

DECL_END

#endif /* !GUARD_LIBREGEX_REGCOMP_C */
