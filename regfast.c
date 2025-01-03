/*[[[magic
options["COMPILE.language"] = "c";
local gcc_opt = options.setdefault("GCC.options", []);
gcc_opt.removeif(x -> x.startswith("-O"));
// Actually: want to optimize to minimize stack memory usage, but this is the next-best thing
gcc_opt.append("-Os");
]]]*/
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
#ifndef GUARD_LIBREGEX_REGFAST_C
#define GUARD_LIBREGEX_REGFAST_C 1
#define _GNU_SOURCE 1
#define _KOS_SOURCE 1
#define LIBREGEX_WANT_PROTOTYPES

#include "api.h"
/**/

#ifndef LIBREGEX_NO_SYSTEM_INCLUDES
#include <hybrid/compiler.h>

#include <hybrid/bitset.h>
#include <hybrid/unaligned.h>

#include <kos/types.h>

#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unicode.h>

#include <libregex/regcomp.h>

#if 0
#include <sys/syslog.h>
#define HAVE_TRACE
#define TRACE(...) syslog(LOG_DEBUG, __VA_ARGS__)
#endif
#endif /* !LIBREGEX_NO_SYSTEM_INCLUDES */

#include "regexec.h"
#include "regfast.h"

#ifndef TRACE
#undef HAVE_TRACE
#define TRACE(...) (void)0
#endif /* !TRACE */

DECL_BEGIN

/* Calculates, and fills in members of `self':
 * - self->rc_fmap
 * - self->rc_minmatch */
#if defined(__OPTIMIZE_SIZE__) && 0
INTERN NONNULL((1)) void
NOTHROW_NCX(CC libre_code_makefast)(struct re_code *__restrict self) {
	self->rc_minmatch = 0;
	bzero(self->rc_fmap, sizeof(self->rc_fmap));
}
#else /* __OPTIMIZE_SIZE__ */

PRIVATE NONNULL((1, 2, 4)) void
NOTHROW_NCX(CC fastmap_setpc)(byte_t fmap[256],
                              struct re_code *__restrict self,
                              byte_t byte, byte_t const *enter_pc) {
	size_t enter_offset = enter_pc - self->rc_code;
	if (enter_offset >= 0xff)
		enter_offset = 0;
	if (fmap[byte] > (byte_t)enter_offset)
		fmap[byte] = (byte_t)enter_offset;
}

PRIVATE NONNULL((1, 2, 5)) void
NOTHROW_NCX(CC fastmap_setpcr)(byte_t fmap[256],
                               struct re_code *__restrict self,
                               byte_t minbyte, byte_t maxbyte,
                               byte_t const *enter_pc) {
	unsigned int i;
	size_t enter_offset = enter_pc - self->rc_code;
	if (enter_offset >= 0xff)
		enter_offset = 0;
	for (i = minbyte; i <= maxbyte; ++i) {
		if (fmap[i] > (byte_t)enter_offset)
			fmap[i] = (byte_t)enter_offset;
	}
}

PRIVATE NONNULL((1, 2, 4)) void
NOTHROW_NCX(CC fastmap_setpc_nbyte)(byte_t fmap[256],
                                    struct re_code *__restrict self,
                                    byte_t nbyte,
                                    byte_t const *enter_pc) {
	unsigned int i;
	size_t enter_offset = enter_pc - self->rc_code;
	if (enter_offset >= 0xff)
		enter_offset = 0;
	for (i = 0; i <= 0xff; ++i) {
		if (i != nbyte) {
			if (fmap[i] > (byte_t)enter_offset)
				fmap[i] = (byte_t)enter_offset;
		}
	}
}

PRIVATE NONNULL((1, 2, 5)) void
NOTHROW_NCX(CC fastmap_setpc_nbyte2)(byte_t fmap[256],
                                     struct re_code *__restrict self,
                                     byte_t nbyte1, byte_t nbyte2,
                                     byte_t const *enter_pc) {
	unsigned int i;
	size_t enter_offset = enter_pc - self->rc_code;
	if (enter_offset >= 0xff)
		enter_offset = 0;
	for (i = 0; i <= 0xff; ++i) {
		if (i != nbyte1 && i != nbyte2) {
			if (fmap[i] > (byte_t)enter_offset)
				fmap[i] = (byte_t)enter_offset;
		}
	}
}

PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_code_setminmatch)(struct re_code *__restrict self,
                                    size_t minmatch) {
	if (self->rc_minmatch > minmatch)
		self->rc_minmatch = minmatch;
}

/* Map `X - RECS_ISX_MIN' to `__UNICODE_IS*' flags. */
#ifndef __libre_unicode_traits_defined
#define __libre_unicode_traits_defined
INTDEF uint16_t const libre_unicode_traits[]; /* from "./regexec.c" */
#endif /* !__libre_unicode_traits_defined */

INTDEF ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) byte_t * /* from "./regcomp.c" */
NOTHROW_NCX(CC libre_opcode_next)(byte_t const *__restrict p_instr);

#define getb() (*pc++)
#define getw() (pc += 2, (int16_t)UNALIGNED_GET16(pc - 2))
#define REQUIRE_MY_VARIABLES() \
	(my_variables || (my_variables = variables = (byte_t *)memcpy(alloca(self->rc_nvars), variables, self->rc_nvars), 1))


PRIVATE NONNULL((1, 2, 3, 4, 5)) void
NOTHROW_NCX(CC populate_fastmap)(byte_t fmap[256],
                                 struct re_code *self,
                                 byte_t *variables,
                                 byte_t const *pc,
                                 byte_t const *enter_pc);

PRIVATE NONNULL((1, 2, 3)) void
NOTHROW_NCX(CC populate_minmatch)(struct re_code *self,
                                  byte_t *variables,
                                  byte_t const *pc,
                                  size_t curr_minmatch,
                                  size_t remaining_depth) {
	byte_t *my_variables = NULL;
	byte_t const *opcode_start;
	byte_t opcode;
again:
	/* Check if we currently discovered shortest branch is already shorter
	 * than what our branch would eventually lead to. If we can't possibly
	 * produce a shorter one that it, then just give up now. */
	if (self->rc_minmatch <= curr_minmatch)
		return;
	opcode_start = pc;
	opcode       = getb();
	switch (opcode) {
#ifdef HAVE_TRACE
#define TARGET(opcode) __IF0 { case opcode: TRACE("%#.4Ix: minmatch: %s [%Iu]\n", (pc - 1) - self->rc_code, #opcode, curr_minmatch); }
#define XTARGET(range) __IF0 {       range: TRACE("%#.4Ix: minmatch: %s [%Iu]\n", (pc - 1) - self->rc_code, #range, curr_minmatch); }
#else /* HAVE_TRACE */
#define TARGET(opcode) case opcode:
#define XTARGET(range) range:
#endif /* !HAVE_TRACE */

		TARGET(REOP_EXACT)
		TARGET(REOP_EXACT_ASCII_ICASE) {
			byte_t len = getb();
			curr_minmatch += len;
			pc += len;
			goto again;
		}

		TARGET(REOP_EXACT_UTF8_ICASE) {
			byte_t len = getb();
			curr_minmatch += len;
			pc = libre_opcode_next(opcode_start);
			goto again;
		}

		XTARGET(case_REOP_ANY_MIN_to_MAX) {
			curr_minmatch += 1;
			goto again;
		}

		TARGET(REOP_BYTE)
		TARGET(REOP_NBYTE) {
			curr_minmatch += 1;
			pc += 1;
			goto again;
		}

		TARGET(REOP_BYTE2)
		TARGET(REOP_NBYTE2)
		TARGET(REOP_RANGE)
		TARGET(REOP_NRANGE) {
			curr_minmatch += 1;
			pc += 2;
			goto again;
		}

		/* All of these opcode always match (at least) 1 byte. */
		TARGET(REOP_CONTAINS_UTF8)
		TARGET(REOP_NCONTAINS_UTF8)
		TARGET(REOP_CS_BYTE)
		TARGET(REOP_CS_UTF8)
		TARGET(REOP_NCS_UTF8) {
			curr_minmatch += 1;
			pc = libre_opcode_next(opcode_start);
			goto again;
		}

		TARGET(REOP_GROUP_MATCH)
		XTARGET(case_REOP_GROUP_MATCH_JMIN_to_JMAX) {
			/* Not  quite correct, but we don't keep track of the minimal match length of past groups.
			 * If we did, then we could just do `curr_minmatch += groups[gid].min_length', and move on
			 * with  the next opcode. (including special handling for when the group was able to match
			 * epsilon) */
			re_code_setminmatch(self, curr_minmatch);
			return;
		}

		XTARGET(case_REOP_AT_MIN_to_MAX)
		TARGET(REOP_POP_ONFAIL)
		TARGET(REOP_JMP_ONFAIL_DUMMY) {
			goto again;
		}

		TARGET(REOP_POP_ONFAIL_AT)
		TARGET(REOP_JMP_ONFAIL_DUMMY_AT) {
			pc += 2;
			goto again;
		}

		TARGET(REOP_GROUP_START)
		TARGET(REOP_GROUP_END)
		XTARGET(case_REOP_GROUP_END_JMIN_to_JMAX) {
			(void)getb(); /* gid */
			goto again;
		}

		TARGET(REOP_JMP_ONFAIL)
		TARGET(REOP_JMP_AND_RETURN_ONFAIL) {
			int16_t delta = getw();
			if (delta <= 0)
				goto again; /* Ignore backwards offsets (those would mean repetition of an epsilon-block) */
			/* Check what the on-fail branch can do for us... */
			if (remaining_depth <= 0) /* Prevent stack-overflow errors */
				goto set_current_minmatch;
			populate_minmatch(self, variables, pc + delta,
			                  curr_minmatch, remaining_depth - 1);
			goto again;
		}

		TARGET(REOP_JMP) {
			int16_t delta = getw();
			assertf(delta >= 0, "The compiler shouldn't produce negative deltas in unconditional jumps");
			pc += delta;
			goto again;
		}

		TARGET(REOP_DEC_JMP_AND_RETURN_ONFAIL) {
			byte_t varid  = getb();
			int16_t delta = getw();
			if (delta <= 0) {
				/* A backwards offsets implies an epsilon-block
				 * -> follow the path where we don't jump backwards. */
				REQUIRE_MY_VARIABLES();
				variables[varid] = 0;
				goto again;
			}

			if (variables[varid] != 0) {
				REQUIRE_MY_VARIABLES();
				--variables[varid];
				if (remaining_depth <= 0) /* Prevent stack-overflow errors */
					goto set_current_minmatch;
				populate_minmatch(self, variables, pc + delta,
				                  curr_minmatch, remaining_depth - 1);
			}
			goto again;
		}

		TARGET(REOP_DEC_JMP) {
			byte_t varid  = getb();
			int16_t delta = getw();
			if (variables[varid] != 0) {
				REQUIRE_MY_VARIABLES();
				--variables[varid];
				pc += delta;
			}
			goto again;
		}

		TARGET(REOP_SETVAR) {
			byte_t varid = getb();
			byte_t value = getb();
			REQUIRE_MY_VARIABLES();
			variables[varid] = value;
			goto again;
		}

		TARGET(REOP_NOP) {
			goto again;
		}

		TARGET(REOP_MATCHED)
		TARGET(REOP_MATCHED_PERFECT) {
set_current_minmatch:
			re_code_setminmatch(self, curr_minmatch);
			return;
		}

	default: __builtin_unreachable();
#undef XTARGET
#undef TARGET
	}
	__builtin_unreachable();
}


PRIVATE ATTR_NOINLINE NONNULL((1, 2, 3, 4, 5, 6)) void
NOTHROW_NCX(CC populate_fastmap_bibranch)(byte_t fmap[256],
                                          struct re_code *self,
                                          byte_t *variables,
                                          byte_t const *yfail_pc,
                                          byte_t const *nfail_pc,
                                          byte_t const *branch_pc) {
	unsigned int i;
	byte_t fmap_yfail[256];
	byte_t fmap_nfail[256];

	/* Generate fast-maps for  both branches of  the onfail  jump.
	 * In  those cases where  exactly 1 of  the 2 branches defines
	 * a fast-map offset, we can set that offset in the final map.
	 *
	 * In those cases where both branches define a fast-map offset
	 * for  the same character,  we have to set  the offset of the
	 * branching `REOP_JMP_ONFAIL' opcode  in the final  fast-map. */
	memset(fmap_yfail, 0xff, sizeof(fmap_yfail));
	memset(fmap_nfail, 0xff, sizeof(fmap_nfail));
	populate_fastmap(fmap_yfail, self, variables, yfail_pc, yfail_pc);
	populate_fastmap(fmap_nfail, self, variables, nfail_pc, nfail_pc);
	for (i = 0; i < 256; ++i) {
		byte_t yfail_offset = fmap_yfail[i];
		byte_t nfail_offset = fmap_nfail[i];
		if (yfail_offset != 0xff) {
			if (nfail_offset != 0xff) {
				/* Both branches accepts this byte -> the branch is needed! */
				fastmap_setpc(fmap, self, (byte_t)i, branch_pc);
			} else {
				/* Only the yfail branch accepts this byte -> jump-ahead */
				fastmap_setpc(fmap, self, (byte_t)i, yfail_pc);
			}
		} else if (nfail_offset != 0xff) {
			/* Only the nfail branch accepts this byte -> jump-ahead */
			fastmap_setpc(fmap, self, (byte_t)i, nfail_pc);
		}
	}
}

PRIVATE ATTR_RETNONNULL WUNUSED NONNULL((1, 2)) byte_t const *
NOTHROW_NCX(CC cs_gather_matching_bytes)(bitset_t matchend_bytes[],
                                         byte_t const *__restrict pc,
                                         bool is_unicode) {
	byte_t cs_opcode;
	while ((cs_opcode = *pc++) != RECS_DONE) {
		switch (cs_opcode) {

		case RECS_CHAR: {
			bitset_set(matchend_bytes, pc[0]);
			if (is_unicode) {
				pc += unicode_utf8seqlen[*pc];
			} else {
				pc += 1;
			}
		}	break;

		case RECS_CHAR2:
			if (is_unicode) {
				/* Match the relevant utf-8 lead bytes */
				bitset_set(matchend_bytes, *pc);
				pc += unicode_utf8seqlen[*pc];
				bitset_set(matchend_bytes, *pc);
				pc += unicode_utf8seqlen[*pc];
			} else {
				bitset_set(matchend_bytes, pc[0]);
				bitset_set(matchend_bytes, pc[1]);
				pc += 2;
			}
			break;
		case RECS_RANGE_ICASE:
			assert(is_unicode);
			ATTR_FALLTHROUGH
		case RECS_RANGE:
			if (is_unicode) {
				char32_t lo, hi;
				lo = unicode_readutf8((char const **)&pc);
				hi = unicode_readutf8((char const **)&pc);
				if (hi >= 0x80) {
					/* Match all utf-8 lead bytes */
					bitset_nset(matchend_bytes, 0xc0, 0xff + 1);
					hi = 0x7f;
				}
				if (lo < 0x80)
					bitset_nset(matchend_bytes, lo, hi + 1);
			} else {
				bitset_nset(matchend_bytes, pc[0], pc[1] + 1);
				pc += 2;
			}
			break;

		case RECS_CONTAINS: {
			byte_t len = *pc++;
			assert(len >= 1);
			if (is_unicode) {
				/* Match all possible utf-8 lead bytes */
				do {
					bitset_set(matchend_bytes, (byte_t)*pc);
					pc += unicode_utf8seqlen[*pc];
				} while (--len);
			} else {
				do {
					bitset_set(matchend_bytes, pc[0]);
					pc += 1;
				} while (--len);
			}
		}	break;

		case_RECS_ISX_MIN_to_MAX: {
			byte_t i;
			uint16_t traits;
			if (!is_unicode)
				goto do_cs_bitset;
			traits = libre_unicode_traits[cs_opcode - RECS_ISX_MIN];
			assertf(is_unicode, "Trait opcodes are only valid in unicode-mode");
			for (i = 0; i < 0x80; ++i) {
				uint16_t ch_flags;
				ch_flags = __unicode_descriptor((char32_t)i)->__ut_flags;
				ch_flags &= traits;
				if (ch_flags != 0) {
					bitset_set(matchend_bytes, i);
				}
			}
		}	break;

		default:
			__pragma_GCC_diagnostic_push_ignored(Wtype_limits)
			if (cs_opcode >= RECS_BITSET_MIN &&
			    cs_opcode <= (is_unicode ? RECS_BITSET_MAX_UTF8
			                             : RECS_BITSET_MAX_BYTE)) {
				uint8_t minch, bitset_size;
				unsigned int i, bitset_bits;
do_cs_bitset:
				minch       = RECS_BITSET_GETBASE(cs_opcode);
				bitset_size = RECS_BITSET_GETBYTES(cs_opcode);
				bitset_bits = bitset_size * 8;
				for (i = 0; i < bitset_bits; ++i) {
					if ((pc[i / 8] & (1 << (i % 8))) != 0) {
						bitset_set(matchend_bytes, (byte_t)(i + minch));
					}
				}
				pc += bitset_size;
				break;
			}
			__pragma_GCC_diagnostic_pop_ignored(Wtype_limits)
			__builtin_unreachable();
		}
	}
	return pc;
}

PRIVATE NONNULL((1, 2, 3, 4, 5)) void
NOTHROW_NCX(CC populate_fastmap)(byte_t fmap[256],
                                 struct re_code *self,
                                 byte_t *variables,
                                 byte_t const *pc,
                                 byte_t const *enter_pc) {
	byte_t *my_variables = NULL;
	byte_t const *opcode_start;
	size_t minmatch;
	byte_t opcode;
again:
	opcode_start = pc;
	opcode       = getb();
	switch (opcode) {
#ifdef HAVE_TRACE
#define TARGET(opcode) __IF0 { case opcode: TRACE("%#.4Ix: fastmap: %s\n", (pc - 1) - self->rc_code, #opcode); }
#define XTARGET(range) __IF0 {       range: TRACE("%#.4Ix: fastmap: %s\n", (pc - 1) - self->rc_code, #range); }
#else /* HAVE_TRACE */
#define TARGET(opcode) case opcode:
#define XTARGET(range) range:
#endif /* !HAVE_TRACE */
#define GOTMATCH() goto got_match

		TARGET(REOP_EXACT) {
			byte_t len = getb();
			fastmap_setpc(fmap, self, pc[0], enter_pc);
			minmatch = len;
			pc += len;
			GOTMATCH();
		}

		TARGET(REOP_EXACT_ASCII_ICASE) {
			byte_t len = getb();
			unsigned char ch = (unsigned char)pc[0];
			fastmap_setpc(fmap, self, (byte_t)tolower(ch), enter_pc);
			fastmap_setpc(fmap, self, (byte_t)toupper(ch), enter_pc);
			minmatch = len;
			pc += len;
			GOTMATCH();
		}

		TARGET(REOP_EXACT_UTF8_ICASE) {
			byte_t len = getb();
			unsigned char ch = (unsigned char)pc[0];
			if (ch >= 0xc0) {
				fastmap_setpcr(fmap, self, 0xc0, 0xff, enter_pc);
			} else {
				fastmap_setpc(fmap, self, (byte_t)tolower(ch), enter_pc);
				fastmap_setpc(fmap, self, (byte_t)toupper(ch), enter_pc);
			}
			minmatch = len;
			pc = libre_opcode_next(opcode_start);
			GOTMATCH();
		}

#if defined(REOP_ANY) || defined(REOP_ANY_UTF8)
#ifdef REOP_ANY
		TARGET(REOP_ANY)
#endif /* REOP_ANY */
#ifdef REOP_ANY_UTF8
		TARGET(REOP_ANY_UTF8)
#endif /* REOP_ANY_UTF8 */
		{
			fastmap_setpcr(fmap, self, 0x00, 0xff, enter_pc);
			minmatch = 1;
			GOTMATCH();
		}
#endif /* REOP_ANY || REOP_ANY_UTF8 */

#if defined(REOP_ANY_NOTLF) || defined(REOP_ANY_NOTLF_UTF8)
#ifdef REOP_ANY_NOTLF
		TARGET(REOP_ANY_NOTLF)
#endif /* REOP_ANY_NOTLF */
#ifdef REOP_ANY_NOTLF_UTF8
		TARGET(REOP_ANY_NOTLF_UTF8)
#endif /* REOP_ANY_NOTLF_UTF8 */
		{
			fastmap_setpcr(fmap, self, 0x00 + 0, 0x0a - 1, enter_pc);
			fastmap_setpcr(fmap, self, 0x0a + 1, 0x0d - 1, enter_pc);
			fastmap_setpcr(fmap, self, 0x0d + 1, 0xff + 0, enter_pc);
			minmatch = 1;
			GOTMATCH();
		}
#endif /* REOP_ANY_NOTLF || REOP_ANY_NOTLF_UTF8 */

#if defined(REOP_ANY_NOTNUL) || defined(REOP_ANY_NOTNUL_UTF8)
#ifdef REOP_ANY_NOTNUL
		TARGET(REOP_ANY_NOTNUL)
#endif /* REOP_ANY_NOTNUL */
#ifdef REOP_ANY_NOTNUL_UTF8
		TARGET(REOP_ANY_NOTNUL_UTF8)
#endif /* REOP_ANY_NOTNUL_UTF8 */
		{
			fastmap_setpcr(fmap, self, 0x01, 0xff, enter_pc);
			minmatch = 1;
			GOTMATCH();
		}
#endif /* REOP_ANY_NOTLF || REOP_ANY_NOTNUL_UTF8 */

#if defined(REOP_ANY_NOTNUL_NOTLF) || defined(REOP_ANY_NOTNUL_NOTLF_UTF8)
#ifdef REOP_ANY_NOTNUL_NOTLF
		TARGET(REOP_ANY_NOTNUL_NOTLF)
#endif /* REOP_ANY_NOTNUL_NOTLF */
#ifdef REOP_ANY_NOTNUL_NOTLF_UTF8
		TARGET(REOP_ANY_NOTNUL_NOTLF_UTF8)
#endif /* REOP_ANY_NOTNUL_NOTLF_UTF8 */
		{
			fastmap_setpcr(fmap, self, 0x01 + 0, 0x0a - 1, enter_pc);
			fastmap_setpcr(fmap, self, 0x0a + 1, 0x0d - 1, enter_pc);
			fastmap_setpcr(fmap, self, 0x0d + 1, 0xff + 0, enter_pc);
			minmatch = 1;
			GOTMATCH();
		}
#endif /* REOP_ANY_NOTLF || REOP_ANY_NOTNUL_NOTLF_UTF8 */

		TARGET(REOP_BYTE) {
			fastmap_setpc(fmap, self, pc[0], enter_pc);
			minmatch = 1;
			pc += 1;
			GOTMATCH();
		}

		TARGET(REOP_NBYTE) {
			fastmap_setpc_nbyte(fmap, self, pc[0], enter_pc);
			minmatch = 1;
			pc += 1;
			GOTMATCH();
		}

		TARGET(REOP_BYTE2) {
			fastmap_setpc(fmap, self, pc[0], enter_pc);
			fastmap_setpc(fmap, self, pc[1], enter_pc);
			minmatch = 1;
			pc += 2;
			GOTMATCH();
		}

		TARGET(REOP_NBYTE2) {
			fastmap_setpc_nbyte2(fmap, self, pc[0], pc[1], enter_pc);
			minmatch = 1;
			pc += 2;
			GOTMATCH();
		}

		TARGET(REOP_RANGE) {
			fastmap_setpcr(fmap, self, pc[0], pc[1], enter_pc);
			minmatch = 1;
			pc += 2;
			GOTMATCH();
		}

		TARGET(REOP_NRANGE) {
			if (pc[0] > 0x00)
				fastmap_setpcr(fmap, self, 0x00, pc[0] - 1, enter_pc);
			if (pc[1] < 0xff)
				fastmap_setpcr(fmap, self, pc[1] + 1, 0xff, enter_pc);
			minmatch = 1;
			pc += 2;
			GOTMATCH();
		}

		TARGET(REOP_CONTAINS_UTF8) {
			byte_t count = getb();
			assert(count >= 2);
			do {
				fastmap_setpc(fmap, self, *pc, enter_pc);
				pc += unicode_utf8seqlen[*pc];
			} while (--count);
			minmatch = 1;
			GOTMATCH();
		}

		TARGET(REOP_NCONTAINS_UTF8) {
			size_t bitno;
			byte_t count = getb();
			bitset_t bitset_decl(acepted_bytes, 256);
			bitset_setall(acepted_bytes, 256);
			assert(count >= 1);
			do {
				bitset_clear(acepted_bytes, *pc);
				pc += unicode_utf8seqlen[*pc];
			} while (--count);
			bitset_foreach (bitno, acepted_bytes, 256) {
				fastmap_setpc(fmap, self, (byte_t)bitno, enter_pc);
			}
			minmatch = 1;
			GOTMATCH();
		}

		TARGET(REOP_CS_BYTE)
		TARGET(REOP_CS_UTF8)
		TARGET(REOP_NCS_UTF8) {
			size_t i;
			bitset_t bitset_decl(matchend_bytes, 256);
			bitset_clearall(matchend_bytes, 256);
			pc = cs_gather_matching_bytes(matchend_bytes, pc,
			                              opcode == REOP_CS_UTF8 ||
			                              opcode == REOP_NCS_UTF8);
			switch (opcode) {
			case REOP_CS_BYTE:
			case REOP_CS_UTF8:
				break;
			case REOP_NCS_UTF8:
				/* Flip the meaning of matched bytes. */
				bitset_flipall(matchend_bytes, 256);
				/* Always match all unicode bytes, since 1 lead byte can match many actual characters. */
				bitset_nset(matchend_bytes, 0xc0, 0xff + 1);
				break;
			default: __builtin_unreachable();
			}
			bitset_foreach (i, matchend_bytes, 256) {
				fastmap_setpc(fmap, self, (byte_t)i, enter_pc);
			}
			minmatch = 1;
			GOTMATCH();
		}

		TARGET(REOP_GROUP_MATCH) {
			/* We only get here due to epsilon-branches, so
			 * a  group repeat also always matches epsilon. */
			goto again;
		}

		XTARGET(case_REOP_GROUP_MATCH_JMIN_to_JMAX) {
			/* A super-early group match can only mean an epsilon-group,
			 * so  we can unconditionally follow the special epsilon-jmp */
			pc += REOP_GROUP_MATCH_Joff(opcode);
			goto again;
		}

		XTARGET(case_REOP_AT_MIN_to_MAX)
		TARGET(REOP_JMP_ONFAIL_DUMMY)
		TARGET(REOP_POP_ONFAIL){
			/* Note how we don't adjust `enter_pc' here! */
			goto again;
		}

		TARGET(REOP_JMP_ONFAIL_DUMMY_AT)
		TARGET(REOP_POP_ONFAIL_AT) {
			/* Note how we don't adjust `enter_pc' here! */
			pc += 2;
			goto again;
		}

		TARGET(REOP_GROUP_START)
		TARGET(REOP_GROUP_END) {
			/* Note how we don't adjust `enter_pc' here! */
			pc += 1;
			goto again;
		}

		XTARGET(case_REOP_GROUP_END_JMIN_to_JMAX) {
			/* A  super-early group-end can only mean an epsilon-group,
			 * so we can unconditionally follow the special epsilon-jmp */
			pc += 1;
			pc += REOP_GROUP_END_Joff(opcode);
			goto again;
		}

		TARGET(REOP_JMP_ONFAIL) {
			byte_t const *yfail_pc;
			byte_t const *nfail_pc;
			int16_t delta = getw();
			if (delta <= 0)
				goto again; /* Ignore backwards offsets (those would mean repetition of an epsilon-block) */
			if (enter_pc != opcode_start) {
				/* Not allowed to do jump-ahead optimizations */
				populate_fastmap(fmap, self, variables, pc + delta, enter_pc);
				goto again;
			}

			yfail_pc = pc + delta;
			nfail_pc = pc;
			populate_fastmap_bibranch(fmap, self, variables, yfail_pc, nfail_pc, opcode_start);
			return;
		}

		TARGET(REOP_JMP) {
			int16_t delta = getw();
			assertf(delta >= 0, "The compiler shouldn't produce negative deltas in unconditional jumps");
			pc += delta;
			if (enter_pc == opcode_start)
				enter_pc = pc;
			goto again;
		}

		TARGET(REOP_JMP_AND_RETURN_ONFAIL) {
			byte_t const *yfail_pc;
			byte_t const *nfail_pc;
			int16_t delta = getw();
			if (delta <= 0)
				goto again; /* Ignore backwards offsets (those would mean repetition of an epsilon-block) */
			if (enter_pc != opcode_start) {
				/* Not allowed to do jump-ahead optimizations */
				populate_fastmap(fmap, self, variables, pc + delta, enter_pc);
				goto again;
			}

			yfail_pc = pc;
			nfail_pc = pc + delta;
			populate_fastmap_bibranch(fmap, self, variables, yfail_pc, nfail_pc, opcode_start);
			return;
		}

		TARGET(REOP_DEC_JMP)
		TARGET(REOP_DEC_JMP_AND_RETURN_ONFAIL) {
			byte_t varid = getb();
			int16_t delta = getw();
			if (delta <= 0) {
				/* A backwards offsets implies an epsilon-block
				 * -> follow the path where we don't jump backwards. */
				REQUIRE_MY_VARIABLES();
				variables[varid] = 0;
				goto again;
			}

			/* Variables have side-effects, so we can't do `populate_fastmap_bibranch()' */
			if (variables[varid] != 0) {
				REQUIRE_MY_VARIABLES();
				--variables[varid];
				populate_fastmap(fmap, self, variables, pc + delta, enter_pc);
			}
			goto again;
		}

		TARGET(REOP_SETVAR) {
			byte_t varid = getb();
			byte_t value = getb();
			REQUIRE_MY_VARIABLES();
			variables[varid] = value;
			goto again;
		}

		TARGET(REOP_NOP) {
			/* NOP has no side-effects, so it can be skipped by the fast-map */
			if (enter_pc == opcode_start)
				enter_pc = pc;
			goto again;
		}

		TARGET(REOP_MATCHED)
		TARGET(REOP_MATCHED_PERFECT) {
			/* If we manage to reach these opcodes before any one of  the
			 * matching instructions, that means that the regex itself is
			 * able to match epsilon.
			 *
			 * -> remember that fact. */
			self->rc_minmatch = 0;
			return;
		}

	default: __builtin_unreachable();
#undef TARGET
	}

	{
		/* We get here after having reached a byte-match of some kind.
		 * With that in mind, we can now proceed to optimize the  min-
		 * match attribute of the code.
		 *
		 * Note that it is OK */
		size_t max_minmatch_depth;
got_match:
		/* The max recursion for calculating the min-match attribute.
		 *
		 * Technically,  there's nothing stopping us from just always
		 * setting an infinite depth as maximum, and that would still
		 * give us a guaranty  that the calculation would  terminate.
		 * However, we might get  a hard stack-overflow error  before
		 * we get that far, so we do still have this additional limit */
		max_minmatch_depth = 16 + (512 / (self->rc_nvars + 1));
		populate_minmatch(self, variables, pc, minmatch, max_minmatch_depth);
	}
}

#undef REQUIRE_MY_VARIABLES
#undef getw
#undef getb

INTERN NONNULL((1)) void
NOTHROW_NCX(CC libre_code_makefast)(struct re_code *__restrict self) {
	byte_t *variables;

	/* Set max limits so these can be lowered by the fastmap code. */
	self->rc_minmatch = SIZE_MAX;
	memset(self->rc_fmap, 0xff, sizeof(self->rc_fmap));

	/* Allocate a buffer for variables. */
	variables = (byte_t *)alloca(self->rc_nvars);

	/* Do the fastmap population (and minmatch calculation) */
	populate_fastmap(self->rc_fmap, self, variables,
	                 self->rc_code, self->rc_code);
	assertf(self->rc_minmatch != SIZE_MAX,
	        "minmatch attribute was never overwritten");
}
#endif /* !__OPTIMIZE_SIZE__ */

#undef HAVE_TRACE
#undef TRACE

DECL_END

#endif /* !GUARD_LIBREGEX_REGFAST_C */
