/*[[[magic
// Compile as `c', so we can use the "register" keyword for optimization hints
options["COMPILE.language"] = "c";
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
#ifndef GUARD_LIBREGEX_REGEXEC_C
#define GUARD_LIBREGEX_REGEXEC_C 1
#define _KOS_SOURCE 1
#define _GNU_SOURCE 1
#define LIBREGEX_WANT_PROTOTYPES

#include "api.h"
/**/

#ifndef LIBREGEX_NO_SYSTEM_INCLUDES
#include <hybrid/compiler.h>

#include <hybrid/minmax.h>
#include <hybrid/overflow.h>
#include <hybrid/unaligned.h>

#include <bits/os/iovec.h>

#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unicode.h>

#include <libregex/regcomp.h>
#include <libregex/regexec.h>

#if 0
#include <sys/syslog.h>
#define HAVE_TRACE
#define TRACE(...) syslog(LOG_DEBUG, __VA_ARGS__)
#endif
#endif /* !LIBREGEX_NO_SYSTEM_INCLUDES */

#include "regexec.h"

#ifndef TRACE
#undef HAVE_TRACE
#define TRACE(...) (void)0
#endif /* !TRACE */

DECL_BEGIN

#define ascii_islf(ch) ((ch) == '\r' || (ch) == '\n')

#if !defined(NDEBUG) && !defined(NDEBUG_FINI)
#define DBG_memset(p, c, n) memset(p, c, n)
#else /* !NDEBUG && !NDEBUG_FINI */
#define DBG_memset(p, c, n) (void)0
#endif /* NDEBUG || NDEBUG_FINI */

#define delta16_get(p) ((int16_t)UNALIGNED_GET16(p))

#define RE_ONFAILURE_ITEM_DUMMY_INPTR                         ((byte_t const *)512) /* == 256 * 2 (256 being the max # of groups per pattern, and 2 being the # of offsets per group) */
#define RE_ONFAILURE_ITEM_SPECIAL_CHECK(inptr)                ((uintptr_t)(inptr) <= (uintptr_t)RE_ONFAILURE_ITEM_DUMMY_INPTR)
#define RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(inptr)          ((uintptr_t)(inptr) < (uintptr_t)RE_ONFAILURE_ITEM_DUMMY_INPTR)
#define RE_ONFAILURE_ITEM_GROUP_RESTORE_ISSTART(inptr)        ((uintptr_t)(inptr) & 1)
#define RE_ONFAILURE_ITEM_GROUP_RESTORE_GETGID(inptr)         ((uint8_t)((uintptr_t)(inptr) >> 1))
#define RE_ONFAILURE_ITEM_GROUP_RESTORE_ENCODE(is_start, gid) ((byte_t const *)(uintptr_t)(((gid) << 1) | (is_start)))

struct re_onfailure_item {
	byte_t const *rof_in; /* [0..1] Input data pointer to restore (points into some input buffer)
	                       * - Set to `RE_ONFAILURE_ITEM_DUMMY_INPTR' for dummy on-fail items.
	                       * - Set to `RE_ONFAILURE_ITEM_GROUP_RESTORE_ENCODE()' if `rof_pc' encodes
	                       *   the start- or end-offset that should be restored for a group on fail. */
	byte_t const *rof_pc; /* [1..1] Program counter to restore
	                       * NOTE: only used for identification when `rof_in == RE_ONFAILURE_ITEM_DUMMY_INPTR' */
};

struct re_interpreter_inptr {
	byte_t const       *ri_in_ptr;    /* [<= ri_in_cend] Current input pointer. */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	byte_t const       *ri_in_cend;   /* End of the current input chunk */
	byte_t const       *ri_in_cbase;  /* [0..1] Original base pointer of current input IOV chunk */
	byte_t const       *ri_in_vbase;  /* [0..1] Virtual base pointer of current input IOV chunk (such that `ri_in_ptr - ri_in_vbase'
	                                   * produces offsets compatible with `struct re_exec::rx_startoff' and the like, including  the
	                                   * start/end offsets stored in `ri_pmatch') */
	struct iovec const *ri_in_miov;   /* [0..*] Further input chunks that have yet to be loaded. (current chunk originates from `ri_in_miov[-1]') */
	size_t              ri_in_mcnt;   /* # of remaining bytes in buffers described by `ri_in_miov[*]' */
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
};

struct re_interpreter {
	union {
		struct re_interpreter_inptr ri_in;        /* Input pointer controller. */
		struct {
			byte_t const           *ri_in_ptr;    /* [<= ri_in_cend] Current input pointer. */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
			byte_t const           *ri_in_cend;   /* End of the current input chunk */
			byte_t const           *ri_in_cbase;  /* [0..1] Original base pointer of current input IOV chunk */
			byte_t const           *ri_in_vbase;  /* [0..1] Virtual base pointer of current input IOV chunk (such that `ri_in_ptr - ri_in_vbase'
			                                       * produces offsets compatible with `struct re_exec::rx_startoff' and the like, including  the
			                                       * start/end offsets stored in `ri_pmatch') */
			struct iovec const     *ri_in_miov;   /* [0..*] Further input chunks that have yet to be loaded. (current chunk originates from `ri_in_miov[-1]') */
			size_t                  ri_in_mcnt;   /* # of remaining bytes in buffers described by `ri_in_miov[*]' */
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
		}; /* TODO: Support for no-transparent-struct */
	};     /* TODO: Support for no-transparent-union */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define ri_in_vbase ri_in_cbase
	byte_t const                   *ri_in_cbase;  /* [== ri_exec->rx_inbase] */
	byte_t const                   *ri_in_cend;   /* [== ri_exec->rx_inbase + ri_exec->rx_endoff] */
	byte_t const                   *ri_in_vend;   /* [== ri_exec->rx_inbase + ri_exec->rx_insize] */
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
	struct iovec const             *ri_in_biov;   /* [0..*][<= ri_in_miov][const] Initial iov vector base (== `struct re_exec::rx_iov') */
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
	struct re_exec const           *ri_exec;      /* [1..1][const] Regex exec command block. */
	re_regmatch_t                  *ri_pmatch;    /* [1..ri_exec->rx_code->rc_ngrps][const] Group match start/end offset register buffer (owned if caller-provided buffer is too small). */
	struct re_onfailure_item       *ri_onfailv;   /* [0..ri_onfailc][owned(free)] On-failure stack */
	size_t                          ri_onfailc;   /* [<= ri_onfaila] # of elements on the on-failure stack */
	size_t                          ri_onfaila;   /* Allocated # of elements of `ri_onfailv' */
	struct re_interpreter_inptr     ri_bmatch;    /* USED INTERNALLY: pending best match */
	re_regmatch_t                  *ri_bmatch_g;  /* [1..ri_exec->rx_code->rc_ngrps]
	                                               * [valid_if(best_match_isvalid() && ri_exec->rx_nmatch != 0)]
	                                               * Group match buffer for `ri_bmatch' */
	byte_t                          ri_flags;     /* Execution flags (set of `RE_INTERPRETER_F_*') */
#define RE_INTERPRETER_F_NORMAL     0x00          /* NORMAL flags */
#define RE_INTERPRETER_F_RSGRPS     0x01          /* ResetGRouPS (on fail) -- must be set when wanting to re-use the interpreter in searches */
	COMPILER_FLEXIBLE_ARRAY(byte_t, ri_vars);     /* [ri_exec->rx_code->rc_nvars] Space for variables used by code. */
};

#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_inptr_in_advance1(self)  (++(self)->ri_in_ptr)
#define re_interpreter_inptr_in_reverse1(self)  (--(self)->ri_in_ptr)
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_inptr_in_advance1(self)                                                           \
	(unlikely((self)->ri_in_ptr >= (self)->ri_in_cend) ? re_interpreter_inptr_nextchunk(self) : (void)0, \
	 ++(self)->ri_in_ptr)
#define re_interpreter_inptr_in_reverse1(self)                                                            \
	(unlikely((self)->ri_in_ptr <= (self)->ri_in_cbase) ? re_interpreter_inptr_prevchunk(self) : (void)0, \
	 --(self)->ri_in_ptr)
#define re_interpreter_in_chunkcontains(self, inptr) \
	((inptr) >= re_interpreter_in_chunkbase(self) && \
	 (inptr) <= re_interpreter_in_chunkend(self)) /* yes: '<=', because inptr == end-of-chunk means epsilon (in case of the last chunk) */
#define re_interpreter_in_isfirstchunk(self)    ((self)->ri_in_miov <= (self)->ri_in_biov + 1)           /* True if in first chunk */
#define re_interpreter_in_islastchunk(self)     ((self)->ri_in_mcnt <= 0)                                /* True if in last chunk */
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

#define re_interpreter_in_chunk_cangetc(self)   ((self)->ri_in_ptr < (self)->ri_in_cend)                 /* True if not at end of current chunk */
#define re_interpreter_in_chunk_canungetc(self) ((self)->ri_in_ptr > (self)->ri_in_cbase)                /* True if not at start of current chunk */
#define re_interpreter_in_chunkdone(self)       ((size_t)((self)->ri_in_ptr - (self)->ri_in_cbase))      /* Bytes already processed from current chunk */
#define re_interpreter_in_chunkleft(self)       ((size_t)((self)->ri_in_cend - (self)->ri_in_ptr))       /* Bytes left to read from current chunk */
#define re_interpreter_in_chunkbase(self)       ((self)->ri_in_cbase)                                    /* Current chunk base pointer */
#define re_interpreter_in_chunkend(self)        ((self)->ri_in_cend)                                     /* Current chunk end pointer */
#define re_interpreter_in_chunksize(self)       ((size_t)((self)->ri_in_cend - (self)->ri_in_cbase))     /* Total size of current chunk */
#define re_interpreter_in_chunkoffset(self)     ((size_t)((self)->ri_in_cbase - (self)->ri_in_vbase))    /* Offset of the currently loaded chunk */
#define re_interpreter_in_chunkendoffset(self)  ((size_t)((self)->ri_in_cend - (self)->ri_in_vbase))     /* Offset at the end of the currently loaded chunk */
#define re_interpreter_in_curoffset(self)       ((size_t)((self)->ri_in_ptr - (self)->ri_in_vbase))      /* Current offset from start of initial chunk */
#define re_interpreter_in_totalleft(self)       (re_interpreter_in_chunkleft(self) + (self)->ri_in_mcnt) /* Total # of bytes of input left */
#define re_interpreter_in_totalleftX(self)      (re_interpreter_in_totalleft(self) + (self)->ri_exec->rx_extra)

#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_in_curoffset_or_ptr(self) (self)->ri_in_ptr
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_in_curoffset_or_ptr(self) re_interpreter_in_curoffset(self)
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_is_soi(self)  ((self)->ri_in_ptr <= (self)->ri_in_vbase)
#define re_interpreter_is_eoi(self)  ((self)->ri_in_ptr >= (self)->ri_in_cend)
#define re_interpreter_is_eoiX(self) ((self)->ri_in_ptr >= (self)->ri_in_vend)
#define re_interpreter_is_eoi_at_end_of_chunk re_interpreter_is_eoi
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_is_soi(self)  ((self)->ri_in_ptr == (self)->ri_in_vbase) /* Must compare `==' in case `ri_in_vbase' had an underflow (`<' w/o underflow would already be an illegal state!) */
#define re_interpreter_is_eoi(self)  ((self)->ri_in_ptr >= (self)->ri_in_cend && (self)->ri_in_mcnt <= 0)
#define re_interpreter_is_eoiX(self) ((self)->ri_in_ptr >= (self)->ri_in_cend && (self)->ri_in_mcnt <= 0 && (self)->ri_exec->rx_extra <= 0)
#define re_interpreter_is_eoi_at_end_of_chunk(self) ((self)->ri_in_mcnt <= 0)
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

static_assert(offsetof(struct re_interpreter_inptr, ri_in_ptr) == offsetof(struct re_interpreter_inptr, ri_in_ptr));
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
static_assert(offsetof(struct re_interpreter_inptr, ri_in_cend) == offsetof(struct re_interpreter_inptr, ri_in_cend));
static_assert(offsetof(struct re_interpreter_inptr, ri_in_cbase) == offsetof(struct re_interpreter_inptr, ri_in_cbase));
static_assert(offsetof(struct re_interpreter_inptr, ri_in_vbase) == offsetof(struct re_interpreter_inptr, ri_in_vbase));
static_assert(offsetof(struct re_interpreter_inptr, ri_in_miov) == offsetof(struct re_interpreter_inptr, ri_in_miov));
static_assert(offsetof(struct re_interpreter_inptr, ri_in_mcnt) == offsetof(struct re_interpreter_inptr, ri_in_mcnt));
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Load the next chunk into `self' */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_nextchunk(self) \
	re_interpreter_inptr_nextchunk(&(self)->ri_in)
PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_interpreter_inptr_nextchunk)(struct re_interpreter_inptr *__restrict self) {
	struct iovec nextchunk;
	size_t old_chunk_endoffset;
	/* vvv This wouldn't account for `struct re_exec::rx_extra'! */
	/*assertf(self->ri_in_mcnt != 0, "No further chunks can be loaded");*/
	old_chunk_endoffset = re_interpreter_in_chunkendoffset(self);
	do {
		nextchunk = *self->ri_in_miov++;
	} while (nextchunk.iov_len == 0); /* Skip over empty chunks */
	if (nextchunk.iov_len > self->ri_in_mcnt)
		nextchunk.iov_len = self->ri_in_mcnt;
	self->ri_in_mcnt -= nextchunk.iov_len;
	self->ri_in_ptr   = (byte_t const *)nextchunk.iov_base;
	self->ri_in_cend  = (byte_t const *)nextchunk.iov_base + nextchunk.iov_len;
	self->ri_in_cbase = (byte_t const *)nextchunk.iov_base;
	self->ri_in_vbase = (byte_t const *)nextchunk.iov_base - old_chunk_endoffset;
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Load the previous chunk into `self' */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_prevchunk(self) \
	re_interpreter_inptr_prevchunk(&(self)->ri_in)
PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_interpreter_inptr_prevchunk)(struct re_interpreter_inptr *__restrict self) {
	struct iovec prevchunk;
	size_t old_chunk_startoffset;
	size_t new_chunk_startoffset;
	old_chunk_startoffset = re_interpreter_in_chunkoffset(self);
	self->ri_in_mcnt += re_interpreter_in_chunksize(self);
	do {
		prevchunk = self->ri_in_miov[-2]; /* [-1] would be the current chunk... */
		--self->ri_in_miov;
	} while (prevchunk.iov_len == 0); /* Skip over empty chunks */
	new_chunk_startoffset = old_chunk_startoffset - prevchunk.iov_len;
	self->ri_in_ptr   = (byte_t const *)prevchunk.iov_base + prevchunk.iov_len;
	self->ri_in_cend  = (byte_t const *)prevchunk.iov_base + prevchunk.iov_len;
	self->ri_in_cbase = (byte_t const *)prevchunk.iov_base;
	self->ri_in_vbase = (byte_t const *)prevchunk.iov_base - new_chunk_startoffset;
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_advance(self, num_bytes)       (void)((self)->ri_in_ptr += (num_bytes))
#define re_interpreter_inptr_advance(self, num_bytes) (void)((self)->ri_in_ptr += (num_bytes))
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_advance(self, num_bytes) \
	re_interpreter_inptr_advance(&(self)->ri_in, num_bytes)
PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_interpreter_inptr_advance)(struct re_interpreter_inptr *__restrict self,
                                             size_t num_bytes) {
again:
	if likely(num_bytes <= re_interpreter_in_chunkleft(self)) {
		self->ri_in_ptr += num_bytes;
	} else {
		num_bytes -= re_interpreter_in_chunkleft(self);
		re_interpreter_inptr_nextchunk(self);
		goto again;
	}
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_reverse(self, num_bytes)       (void)((self)->ri_in_ptr -= (num_bytes))
#define re_interpreter_inptr_reverse(self, num_bytes) (void)((self)->ri_in_ptr -= (num_bytes))
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_reverse(self, num_bytes) \
	re_interpreter_inptr_reverse(&(self)->ri_in, num_bytes)
PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_interpreter_inptr_reverse)(struct re_interpreter_inptr *__restrict self,
                                             size_t num_bytes) {
again:
	if likely(num_bytes <= re_interpreter_in_chunkdone(self)) {
		self->ri_in_ptr -= num_bytes;
	} else {
		num_bytes -= re_interpreter_in_chunkdone(self);
		re_interpreter_inptr_prevchunk(self);
		goto again;
	}
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */


/* Return the previous byte from input */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_prevbyte(self)       (self)->ri_in_ptr[-1]
#define re_interpreter_inptr_prevbyte(self) (self)->ri_in_ptr[-1]
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_prevbyte(self) \
	(likely(re_interpreter_in_chunk_canungetc(self)) ? (self)->ri_in_ptr[-1] : _re_interpreter_inptr_prevbyte(&(self)->ri_in))
#define re_interpreter_inptr_prevbyte(self) \
	(likely(re_interpreter_in_chunk_canungetc(self)) ? (self)->ri_in_ptr[-1] : _re_interpreter_inptr_prevbyte(self))
PRIVATE WUNUSED NONNULL((1)) byte_t
NOTHROW_NCX(CC _re_interpreter_inptr_prevbyte)(struct re_interpreter_inptr const *__restrict self) {
	struct iovec const *iov;
	iov = self->ri_in_miov - 2; /* -1 would be the current chunk */
	while (iov->iov_len == 0)
		--iov; /* Skip over empty chunks */
	return ((byte_t const *)iov->iov_base)[iov->iov_len - 1];
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Return the next byte that will be read from input */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_nextbyte(self)       (*(self)->ri_in_ptr)
#define re_interpreter_inptr_nextbyte(self) (*(self)->ri_in_ptr)
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_nextbyte(self) \
	(likely(re_interpreter_in_chunk_cangetc(self)) ? *(self)->ri_in_ptr : _re_interpreter_inptr_nextbyte(&(self)->ri_in))
#define re_interpreter_inptr_nextbyte(self) \
	(likely(re_interpreter_in_chunk_cangetc(self)) ? *(self)->ri_in_ptr : _re_interpreter_inptr_nextbyte(self))
PRIVATE WUNUSED NONNULL((1)) byte_t
NOTHROW_NCX(CC _re_interpreter_inptr_nextbyte)(struct re_interpreter_inptr const *__restrict self) {
	struct iovec const *iov;
	/* vvv This wouldn't account for `struct re_exec::rx_extra'! */
	/*assertf(self->ri_in_mcnt != 0, "No further chunks can be loaded");*/
	iov = self->ri_in_miov;
	while (iov->iov_len == 0)
		++iov; /* Skip over empty chunks */
	return *(byte_t const *)iov->iov_base;
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Peek memory that has been read in the past, copying up to `max_bytes' bytes of it into `buf'. */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
PRIVATE WUNUSED NONNULL((1)) size_t
NOTHROW_NCX(CC re_interpreter_peekmem_bck)(struct re_interpreter const *__restrict self,
                                           void *buf, size_t max_bytes) {
	size_t avail, result;
	size_t total_prev = re_interpreter_in_curoffset(self);
	byte_t *dst = (byte_t *)buf + max_bytes;
	if (max_bytes > total_prev)
		max_bytes = total_prev;
	result = max_bytes;
	avail  = re_interpreter_in_chunkdone(self);
	if (avail > max_bytes)
		avail = max_bytes;
	dst -= avail;
	max_bytes -= avail;
	memcpy(dst, self->ri_in_ptr, avail);
	if (max_bytes) {
		struct iovec const *iov = self->ri_in_miov - 2; /* -1 would be the current chunk */
		do {
			avail = iov->iov_len;
			if (avail > max_bytes)
				avail = max_bytes;
			dst -= avail;
			memcpy(dst, (byte_t *)iov->iov_base + iov->iov_len - avail, avail);
			max_bytes -= avail;
			++iov;
		} while (max_bytes);
	}
	if (dst > (byte_t *)buf)
		memmovedown(dst, buf, max_bytes);
	return result;
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Peek memory that will be read in the future, copying up to `max_bytes' bytes of it into `buf'.
 * NOTE: This function also allows access to trailing `rx_extra' extra bytes. */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
PRIVATE WUNUSED NONNULL((1)) size_t
NOTHROW_NCX(CC re_interpreter_peekmem_fwd)(struct re_interpreter const *__restrict self,
                                           void *buf, size_t max_bytes) {
	size_t avail, result;
	size_t total_left = re_interpreter_in_totalleftX(self);
	if (max_bytes > total_left)
		max_bytes = total_left;
	result = max_bytes;
	avail  = re_interpreter_in_chunkleft(self);
	if (avail > max_bytes)
		avail = max_bytes;
	buf = mempcpy(buf, self->ri_in_ptr, avail);
	max_bytes -= avail;
	if (max_bytes) {
		struct iovec const *iov = self->ri_in_miov;
		do {
			avail = iov->iov_len;
			if (avail > max_bytes)
				avail = max_bytes;
			buf = mempcpy(buf, iov->iov_base, avail);
			max_bytes -= avail;
			++iov;
		} while (max_bytes);
	}
	return result;
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Return the previous utf-8 character from input */
PRIVATE WUNUSED NONNULL((1)) char32_t
NOTHROW_NCX(CC re_interpreter_prevutf8)(struct re_interpreter const *__restrict self) {
	if likely(re_interpreter_in_chunk_canungetc(self)) {
		byte_t prevbyte = self->ri_in_ptr[-1];
		if likely(prevbyte < 0x80)
			return prevbyte;
		if likely((self->ri_in_ptr - UNICODE_UTF8_CURLEN) <= self->ri_in_cbase) {
			/* Can just read the entire character from the current chunk */
			char const *reader = (char const *)self->ri_in_ptr;
			return unicode_readutf8_rev(&reader);
		}
	}

#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	if likely(re_interpreter_in_isfirstchunk(self))
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
	{
		/* First chunk -> just read a restricted-length unicode character */
		char const *reader = (char const *)self->ri_in_ptr;
		assert(reader > (char const *)self->ri_in_cbase);
		return unicode_readutf8_rev_n(&reader, (char const *)self->ri_in_cbase);
	}

#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	/* Fallback: copy memory into a temporary buffer. */
	{
		size_t utf8_len;
		char utf8[UNICODE_UTF8_CURLEN], *reader;
		utf8_len = re_interpreter_peekmem_bck(self, utf8, sizeof(utf8));
		reader   = utf8 + utf8_len;
		assert(utf8_len != 0);
		return unicode_readutf8_rev_n((char const **)&reader, utf8);
	}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
}

/* Return the next utf-8 character that will be read from input */
PRIVATE WUNUSED NONNULL((1)) char32_t
NOTHROW_NCX(CC re_interpreter_nextutf8)(struct re_interpreter const *__restrict self) {
	if likely(re_interpreter_in_chunk_cangetc(self)) {
		uint8_t seqlen;
		byte_t nextbyte = *self->ri_in_ptr;
		if likely(nextbyte < 0x80)
			return nextbyte;
		seqlen = unicode_utf8seqlen[nextbyte];
		if unlikely(!seqlen)
			return nextbyte; /* Dangling follow-up byte? */
		if likely((self->ri_in_ptr + seqlen) <= self->ri_in_cend) {
			/* Can just read the entire character from the current chunk */
			char const *reader = (char const *)self->ri_in_ptr;
			return unicode_readutf8(&reader);
		}
	}

#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	if likely(re_interpreter_in_islastchunk(self))
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
	{
		/* Last chunk -> just read a restricted-length unicode character */
		char const *reader = (char const *)self->ri_in_ptr;
		assert(reader < (char const *)self->ri_in_cend);
		return unicode_readutf8_n(&reader, (char const *)self->ri_in_cend);
	}

#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	/* Fallback: copy memory into a temporary buffer. */
	{
		size_t utf8_len;
		char utf8[UNICODE_UTF8_CURLEN], *reader;
		utf8_len = re_interpreter_peekmem_fwd(self, utf8, sizeof(utf8));
		reader   = utf8;
		assert(utf8_len != 0);
		return unicode_readutf8_n((char const **)&reader, utf8 + utf8_len);
	}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
}


/* Read a byte whilst advancing the input pointer. */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_readbyte(self)       (*(self)->ri_in_ptr++)
#define re_interpreter_inptr_readbyte(self) (*(self)->ri_in_ptr++)
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
#define re_interpreter_readbyte(self)                                                              \
	(unlikely((self)->ri_in_ptr >= (self)->ri_in_cend) ? re_interpreter_nextchunk(self) : (void)0, \
	 *(self)->ri_in_ptr++)
#define re_interpreter_inptr_readbyte(self)                                                              \
	(unlikely((self)->ri_in_ptr >= (self)->ri_in_cend) ? re_interpreter_inptr_nextchunk(self) : (void)0, \
	 *(self)->ri_in_ptr++)
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Read a utf-8 character whilst advancing the input pointer. */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_readutf8(self) \
	unicode_readutf8_n((char const **)&(self)->ri_in_ptr, (char const *)(self)->ri_in_cend)
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
PRIVATE NONNULL((1)) char32_t
NOTHROW_NCX(CC re_interpreter_readutf8)(struct re_interpreter *__restrict self) {
	uint8_t seqlen;
	byte_t nextbyte;
	if unlikely(!re_interpreter_in_chunk_cangetc(self))
		re_interpreter_inptr_nextchunk(&self->ri_in);

	nextbyte = *self->ri_in_ptr;
	if likely(nextbyte < 0x80) {
		++self->ri_in_ptr;
		return nextbyte;
	}
	seqlen = unicode_utf8seqlen[nextbyte];
	if unlikely(!seqlen) {
		++self->ri_in_ptr;
		return nextbyte; /* Dangling follow-up byte? */
	}
	if likely((self->ri_in_ptr + seqlen) <= self->ri_in_cend) {
		/* Can just read the entire character from the current chunk */
		return unicode_readutf8((char const **)&self->ri_in_ptr);
	}
	if likely(re_interpreter_in_islastchunk(self)) {
		/* Last chunk -> just read a restricted-length unicode character */
		assert((char const *)self->ri_in_ptr < (char const *)self->ri_in_cend);
		return unicode_readutf8_n((char const **)&self->ri_in_ptr, (char const *)self->ri_in_cend);
	}

	/* Unicode character is spread across multiple chunks */
	{
		size_t firstchunk, missing, left;
		char utf8[UNICODE_UTF8_CURLEN], *dst, *reader;
		firstchunk = re_interpreter_in_chunkleft(self);
		assert(seqlen > firstchunk);
		dst     = (char *)mempcpy(utf8, self->ri_in_ptr, firstchunk);
		missing = seqlen - firstchunk;
		re_interpreter_inptr_nextchunk(&self->ri_in);
		left = re_interpreter_in_totalleft(self);
		if (missing > left)
			missing = left;
		if (missing) {
			for (;;) {
				size_t avail = re_interpreter_in_chunkleft(self);
				if (avail > missing)
					avail = missing;
				dst = (char *)mempcpy(dst, self->ri_in_ptr, avail);
				missing -= avail;
				self->ri_in_ptr += avail;
				if (!missing)
					break;
				re_interpreter_inptr_nextchunk(&self->ri_in);
			}
		}
		reader = utf8;
		return unicode_readutf8_n((char const **)&reader, dst);
	}
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */




/* Given an `inptr' that points into some unknown input data chunk,
 * load said chunk and set the input pointer of `self' to  `inptr'.
 *
 * Behavior is undefined when `inptr' isn't a valid input pointer. */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
#define re_interpreter_setinptr(self, inptr) (void)((self)->ri_in_ptr = (inptr))
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
PRIVATE NONNULL((1, 2)) void
NOTHROW_NCX(CC re_interpreter_setinptr)(struct re_interpreter *__restrict self,
                                        byte_t const *inptr) {
	if (re_interpreter_in_chunkcontains(self, inptr)) {
		/* Simple case: don't need to change chunks */
		self->ri_in_ptr = inptr;
	} else {
		/* Complex case: need to find the chunk that `inptr' belongs to.
		 * -> First off: assume that it's from a past chunk. */
		size_t curr_chunk_start_offset = re_interpreter_in_chunkoffset(self);
		size_t prev_chunk_end_offset   = curr_chunk_start_offset;
		struct iovec const *prev_chunk = self->ri_in_miov - 2;
		while (prev_chunk >= self->ri_in_biov) {
			byte_t *prev_chunk_end = (byte_t *)prev_chunk->iov_base + prev_chunk->iov_len;
			if (inptr >= (byte_t *)prev_chunk->iov_base && inptr <= prev_chunk_end) {
				size_t inptr_offset;

				/* Found the chunk that the given inptr belongs to -> now load it! */
				inptr_offset = prev_chunk_end_offset;
				inptr_offset -= prev_chunk->iov_len;
				inptr_offset += (size_t)(inptr - (byte_t *)prev_chunk->iov_base);
				self->ri_in_mcnt += re_interpreter_in_chunkendoffset(self);
				self->ri_in_mcnt -= prev_chunk_end_offset;
				self->ri_in_ptr   = inptr;
				self->ri_in_cend  = prev_chunk_end;
				self->ri_in_cbase = (byte_t *)prev_chunk->iov_base;
				self->ri_in_vbase = inptr - inptr_offset;
				self->ri_in_miov  = prev_chunk + 1;
				return;
			}

			prev_chunk_end_offset -= prev_chunk->iov_len;
			--prev_chunk;
		}

		/* Pointer must be located some a future chunk. Seek until we find it! */
		do {
			re_interpreter_nextchunk(self);
		} while (!re_interpreter_in_chunkcontains(self, inptr));
		self->ri_in_ptr = inptr;
	}
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

/* Set the absolute offset of `self' */
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
PRIVATE NONNULL((1)) void
NOTHROW_NCX(CC re_interpreter_inptr_setoffset)(struct re_interpreter_inptr *__restrict self,
                                               size_t offset) {
	size_t curr_chunk_start_offset = re_interpreter_in_chunkoffset(self);
	size_t curr_chunk_end_offset   = re_interpreter_in_chunkendoffset(self);
	if (offset >= curr_chunk_start_offset &&
	    offset <= curr_chunk_end_offset) { /* <<< yes: "<=" (in case `offset' points to the end of  the
	                                        *     input buffer, we mustn't load an out-of-bounds chunk) */
		/* Simple case: don't need to change chunks */
		self->ri_in_ptr = self->ri_in_cbase + (offset - curr_chunk_start_offset);
	} else {
		size_t curr_offset = re_interpreter_in_curoffset(self);
		if (offset >= curr_chunk_end_offset) {
			/* Skip ahead */
			assert(offset > curr_offset);
			re_interpreter_inptr_advance(self, offset - curr_offset);
		} else {
			/* Rewind backwards */
			assert(offset < curr_chunk_start_offset);
			assert(offset < curr_offset);
			re_interpreter_inptr_reverse(self, curr_offset - offset);
		}
	}
}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */



/* Allocate a new regex interpreter */
#define re_interpreter_alloc(nvars) \
	((struct re_interpreter *)alloca(offsetof(struct re_interpreter, ri_vars) + (nvars) * sizeof(byte_t)))

/* Initialize a given regex */
#define re_interpreter_fini(self) free((self)->ri_onfailv)
PRIVATE WUNUSED NONNULL((1, 2)) re_errno_t
NOTHROW_NCX(CC re_interpreter_init)(struct re_interpreter *__restrict self,
                                    struct re_exec const *__restrict exec) {
#ifndef LIBREGEX_REGEXEC_SINGLE_CHUNK
	size_t chunkoff = 0;
	size_t in_len;
	struct iovec const *iov = exec->rx_iov;
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
	size_t startoff = exec->rx_startoff;
	size_t endoff   = exec->rx_endoff;
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
	self->ri_in_cbase = (byte_t const *)exec->rx_inbase;
	self->ri_in_cend  = (byte_t const *)exec->rx_inbase + endoff;
	if unlikely(startoff >= endoff) {
		endoff = startoff; /* `startoff' is still needed to get the size of epsilon matches right! */
		self->ri_in_ptr  = self->ri_in_cbase + startoff;
		self->ri_in_vend = self->ri_in_cbase + exec->rx_insize;
		if unlikely(startoff > exec->rx_insize)
			self->ri_in_vend = self->ri_in_ptr; /* Ensure that we start _at_ true EOF (not after it) */
		goto done_in_init;
	}
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
	if unlikely(startoff >= endoff) {
		static struct iovec const empty_iov = { NULL, 0 };
		/* Special case: input buffer is epsilon. */
		chunkoff = startoff; /* `startoff' is still needed to get the size of epsilon matches right! */
		endoff   = 0;
		startoff = 0;
		if (exec->rx_extra != 0)
			goto load_normal_iov;
		iov              = &empty_iov;
		self->ri_in_biov = iov;
	} else {
		/* Seek ahead until the first relevant chunk */
load_normal_iov:
		self->ri_in_biov = iov;
		while (startoff >= iov->iov_len) {
			chunkoff += iov->iov_len;
			startoff -= iov->iov_len;
			endoff -= iov->iov_len;
			++iov;
		}
	}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

	/* Fill in interpreter fields */
	assert(startoff <= endoff);
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
	self->ri_in_ptr  = self->ri_in_cbase + startoff;
	self->ri_in_vend = self->ri_in_cbase + exec->rx_insize;
done_in_init:
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
	self->ri_in_ptr   = (byte_t const *)iov->iov_base + startoff;
	in_len            = iov->iov_len - startoff;
	self->ri_in_cend  = self->ri_in_ptr + in_len;
	self->ri_in_vbase = self->ri_in_ptr - (chunkoff + startoff);
	self->ri_in_cbase = (byte_t const *)iov->iov_base;
	self->ri_in_miov  = iov + 1;
	self->ri_in_mcnt  = endoff - startoff;
	if (self->ri_in_mcnt >= in_len) {
		/* More memory may be had from extra chunks */
		self->ri_in_mcnt -= in_len;
	} else {
		/* All relevant memory is located in a single chunk. */
		in_len           = self->ri_in_mcnt;
		self->ri_in_cend = self->ri_in_ptr + in_len;
		self->ri_in_mcnt = 0;
	}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
	self->ri_exec    = exec;
	self->ri_onfailv = NULL;
	self->ri_onfailc = 0;
	self->ri_onfaila = 0;
	self->ri_flags   = RE_INTERPRETER_F_NORMAL;
	DBG_memset(self->ri_vars, 0xcc, exec->rx_code->rc_nvars * sizeof(byte_t));
	return RE_NOERROR;
}

/* Initialize the match-buffer of `self' */
#define re_interpreter_init_match(self, exec, for_search)                                 \
	do {                                                                                  \
		uint16_t _ngrp;                                                                   \
		(self)->ri_pmatch = (exec)->rx_pmatch;                                            \
		_ngrp             = (exec)->rx_code->rc_ngrps;                                    \
		if ((exec)->rx_nmatch >= _ngrp) {                                                 \
			/* Able to use user-provided register buffer. */                              \
		} else {                                                                          \
			/* Need to use our own group start/end-offset buffer.                         \
			 * NOTE: stack-allocated, because max size is 256*8 = 2048 bytes. */          \
			(self)->ri_pmatch = (re_regmatch_t *)alloca(_ngrp * sizeof(re_regmatch_t));   \
		}                                                                                 \
		/* Set all offsets to RE_REGOFF_UNSET (if they're written back at the end,        \
		 * or if the code being executed expects unset groups to be marked properly) */   \
		if ((exec)->rx_nmatch || ((exec)->rx_code->rc_flags & RE_CODE_FLAG_NEEDGROUPS)) { \
			memsetc((self)->ri_pmatch, RE_REGOFF_UNSET,                                   \
			        _ngrp * 2, sizeof(re_regoff_t));                                      \
			if (for_search) {                                                             \
				/* Interpreter is used for searching and needs groups (reset on fail) */  \
				(self)->ri_flags |= RE_INTERPRETER_F_RSGRPS;                              \
			}                                                                             \
		}                                                                                 \
	}	__WHILE0

/* Copy the matches produced by `self' back into the caller-provided
 * buffer, unless it was the caller-provided buffer that was used by
 * the interpreter. */
#define re_interpreter_copy_match(self)                                       \
	do {                                                                      \
		if ((self)->ri_pmatch != (self)->ri_exec->rx_pmatch) {                \
			/* Must copy over match information into user-provided buffer. */ \
			memcpyc((self)->ri_exec->rx_pmatch,                               \
			        (self)->ri_pmatch,                                        \
			        (self)->ri_exec->rx_nmatch,                               \
			        sizeof(re_regmatch_t));                                   \
		}                                                                     \
	}	__WHILE0


/* The min number of regex failures that are always allowed
 *
 * While the on-fail stack's size is below this, we won't
 * even look at `re_max_failures(3)' */
#define RE_MIN_FAILURES 128

#ifdef LIBREGEX_USED__re_max_failures
#define get_re_max_failures() LIBREGEX_USED__re_max_failures
#else /* LIBREGEX_USED__re_max_failures */
/* This one has to be int-sized for Glibc-compat. Technically, Glibc
 * defines this one as  `int', there's no need  to do that; we  just
 * interpret the variable as unsigned int!
 *
 * The actual symbol itself is defined in libc, but we don't want to
 * hard-link against it, and so simply load it lazily on first  use. */
PRIVATE unsigned int const *pdyn_re_max_failures = NULL;

PRIVATE ATTR_NOINLINE ATTR_PURE WUNUSED size_t
NOTHROW(CC get_re_max_failures)(void) {
	if (pdyn_re_max_failures == NULL) {
		pdyn_re_max_failures = (unsigned int const *)dlsym(RTLD_DEFAULT, "re_max_failures");
		if unlikely(pdyn_re_max_failures == NULL) {
			/* Shouldn't get here, but just to be safe... */
			static unsigned int const default_re_max_failures = 2000;
			pdyn_re_max_failures = &default_re_max_failures;
		}
	}
	return (size_t)*pdyn_re_max_failures;
}
#endif /* !LIBREGEX_USED__re_max_failures */

PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_interpreter_resize)(struct re_interpreter *__restrict self) {
	struct re_onfailure_item *new_onfail_v;
	size_t new_onfail_a;
	/* Must allocate more space for the on-fail buffer. */
	new_onfail_a = self->ri_onfaila * 2;

	if (new_onfail_a < RE_MIN_FAILURES) {
		new_onfail_a = RE_MIN_FAILURES;
	} else {
		/* Limit how many failure items there can be */
		size_t max_count = get_re_max_failures();
		if (new_onfail_a > max_count) {
			new_onfail_a = max_count;
#ifndef LIBREGEX_USED__re_max_failures
			if (new_onfail_a < RE_MIN_FAILURES)
				new_onfail_a = RE_MIN_FAILURES; /* Never go below the minimum */
#endif /* !LIBREGEX_USED__re_max_failures */
			if (self->ri_onfailc >= new_onfail_a)
				return false; /* New limit is too low for current requirements. */
		}
	}

	new_onfail_v = (struct re_onfailure_item *)reallocv(self->ri_onfailv, new_onfail_a,
	                                                    sizeof(struct re_onfailure_item));
	if unlikely(!new_onfail_v) {
		new_onfail_a = self->ri_onfailc + 1;
		new_onfail_v = (struct re_onfailure_item *)reallocv(self->ri_onfailv, new_onfail_a,
		                                                    sizeof(struct re_onfailure_item));
		if unlikely(!new_onfail_v)
			return false; /* Out-of-memory :( */
	}
	self->ri_onfailv = new_onfail_v;
	self->ri_onfaila = new_onfail_a;
	return true;
}

PRIVATE WUNUSED NONNULL((1, 2)) bool
NOTHROW_NCX(CC re_interpreter_pushfail)(struct re_interpreter *__restrict self,
                                        byte_t const *pc) {
	struct re_onfailure_item *item;
	assert(self->ri_onfailc <= self->ri_onfaila);
	if unlikely(self->ri_onfailc >= self->ri_onfaila) {
		if unlikely(!re_interpreter_resize(self))
			return false;
	}
	item = &self->ri_onfailv[self->ri_onfailc++];
	item->rof_in = self->ri_in_ptr;
	item->rof_pc = pc;
	return true;
}

PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_interpreter_pushfail_dummy)(struct re_interpreter *__restrict self,
                                              byte_t const *in, byte_t const *pc) {
	struct re_onfailure_item *item;
	assert(self->ri_onfailc <= self->ri_onfaila);
	if unlikely(self->ri_onfailc >= self->ri_onfaila) {
		if unlikely(!re_interpreter_resize(self))
			return false;
	}
	item = &self->ri_onfailv[self->ri_onfailc++];
	item->rof_in = in;
	item->rof_pc = pc;
	return true;
}

/* Consume a repetition of bytes from `offset...+=num_bytes'
 * - Upon success (repeat was matched), return `true' and leave
 *   the current input pointer of `self' pointing to the end of
 *   the secondary match
 * - Upon failure (repeat wasn't matched), return `false' and
 *   leave the current input pointer of `self' undefined (but
 *   valid)
 * Assumes that `num_bytes > 0' */
PRIVATE WUNUSED NONNULL((1)) bool
NOTHROW_NCX(CC re_interpreter_consume_repeat)(struct re_interpreter *__restrict self,
                                              re_regoff_t offset, size_t num_bytes) {
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
	assert(num_bytes > 0);
	if unlikely(re_interpreter_in_chunkleft(self) < num_bytes)
		return false;
	if (bcmp(self->ri_in_cbase + offset, self->ri_in_ptr, num_bytes) != 0)
		return false;
	re_interpreter_advance(self, num_bytes);
	return true;
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
	struct re_interpreter_inptr srcptr;
	assert(num_bytes > 0);
	srcptr = self->ri_in;
	re_interpreter_inptr_setoffset(&srcptr, offset);
	for (;;) {
		size_t src_bytes, cur_bytes, com_bytes;
		src_bytes = re_interpreter_in_chunkleft(&srcptr);
		if (!src_bytes) {
			re_interpreter_inptr_nextchunk(&srcptr);
			src_bytes = re_interpreter_in_chunkleft(&srcptr);
		}
		cur_bytes = re_interpreter_in_chunkleft(self);
		if (!cur_bytes) {
			re_interpreter_nextchunk(self);
			cur_bytes = re_interpreter_in_chunkleft(self);
		}
		com_bytes = MIN(src_bytes, cur_bytes, num_bytes);
		assert(com_bytes != 0);
		/* Compare memory */
		if (bcmp(srcptr.ri_in_ptr, self->ri_in_ptr, com_bytes) != 0)
			return false;
		num_bytes -= com_bytes;
		if (!num_bytes)
			break;
		srcptr.ri_in_ptr += com_bytes;
		self->ri_in_ptr += com_bytes;
	}
	return true;
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
}


/* Map `X - RECS_ISX_MIN' to `__UNICODE_IS*' flags. */
#define __libre_unicode_traits_defined
INTERN_CONST uint16_t const libre_unicode_traits[] = {
#ifdef __GNUC__
#define DEF_TRAIT(opcode, mask) [(opcode - RECS_ISX_MIN)] = mask
#else /* __GNUC__ */
#define DEF_TRAIT(opcode, mask) /*[(opcode - RECS_ISX_MIN)] =*/ mask
#endif /* !__GNUC__ */
	DEF_TRAIT(RECS_ISCNTRL, __UNICODE_ISCNTRL),     /* `unicode_iscntrl(ch)' */
	DEF_TRAIT(RECS_ISSPACE, __UNICODE_ISSPACE),     /* `unicode_isspace(ch)' */
	DEF_TRAIT(RECS_ISUPPER, __UNICODE_ISUPPER),     /* `unicode_isupper(ch)' */
	DEF_TRAIT(RECS_ISLOWER, __UNICODE_ISLOWER),     /* `unicode_islower(ch)' */
	DEF_TRAIT(RECS_ISALPHA, __UNICODE_ISALPHA),     /* `unicode_isalpha(ch)' */
	DEF_TRAIT(RECS_ISDIGIT, __UNICODE_ISDIGIT),     /* `unicode_isdigit(ch)' */
	DEF_TRAIT(RECS_ISXDIGIT, __UNICODE_ISXDIGIT),   /* `unicode_isxdigit(ch)' */
	DEF_TRAIT(RECS_ISALNUM, __UNICODE_ISALNUM),     /* `unicode_isalnum(ch)' */
	DEF_TRAIT(RECS_ISPUNCT, __UNICODE_ISPUNCT),     /* `unicode_ispunct(ch)' */
	DEF_TRAIT(RECS_ISGRAPH, __UNICODE_ISGRAPH),     /* `unicode_isgraph(ch)' */
	DEF_TRAIT(RECS_ISPRINT, __UNICODE_ISPRINT),     /* `unicode_isprint(ch)' */
	DEF_TRAIT(RECS_ISBLANK, __UNICODE_ISBLANK),     /* `unicode_isblank(ch)' */
	DEF_TRAIT(RECS_ISSYMSTRT, __UNICODE_ISSYMSTRT), /* `unicode_issymstrt(ch)' */
	DEF_TRAIT(RECS_ISSYMCONT, __UNICODE_ISSYMCONT), /* `unicode_issymcont(ch)' */
	DEF_TRAIT(RECS_ISTAB, __UNICODE_ISTAB),         /* `unicode_istab(ch)' */
	DEF_TRAIT(RECS_ISWHITE, __UNICODE_ISWHITE),     /* `unicode_iswhite(ch)' */
	DEF_TRAIT(RECS_ISEMPTY, __UNICODE_ISEMPTY),     /* `unicode_isempty(ch)' */
	DEF_TRAIT(RECS_ISLF, __UNICODE_ISLF),           /* `unicode_islf(ch)' */
	DEF_TRAIT(RECS_ISHEX, __UNICODE_ISHEX),         /* `unicode_ishex(ch)' */
	DEF_TRAIT(RECS_ISTITLE, __UNICODE_ISTITLE),     /* `unicode_istitle(ch)' */
	DEF_TRAIT(RECS_ISNUMERIC, __UNICODE_ISNUMERIC), /* `unicode_isnumeric(ch)' */
#undef DEF_TRAIT
};

PRIVATE ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) byte_t const *
NOTHROW_NCX(CC CS_BYTE_seek_end)(__register byte_t const *__restrict pc) {
	__register byte_t cs_opcode;
again:
	cs_opcode = *pc++;
	switch (cs_opcode) {

	case_RECS_BITSET_MIN_to_MAX_BYTE:
		pc += RECS_BITSET_GETBYTES(cs_opcode);
		goto again;

	case RECS_DONE:
		break;

	case RECS_CHAR:
		pc += 1;
		goto again;

	case RECS_CHAR2:
	case RECS_RANGE:
		pc += 2;
		goto again;

	case RECS_CONTAINS: {
		byte_t len = *pc++;
		assert(len >= 1);
		pc += len;
		goto again;
	}

		/* No need to handle trait opcodes (those aren't valid in byte-mode) */

	default: __builtin_unreachable();
	}
	return pc;
}

PRIVATE ATTR_PURE ATTR_RETNONNULL WUNUSED NONNULL((1)) byte_t const *
NOTHROW_NCX(CC CS_UTF8_seek_end)(__register byte_t const *__restrict pc) {
	__register byte_t cs_opcode;
again:
	cs_opcode = *pc++;
	switch (cs_opcode) {

	case_RECS_BITSET_MIN_to_MAX_UTF8:
		pc += RECS_BITSET_GETBYTES(cs_opcode);
		goto again;

	case RECS_DONE:
		break;

	case RECS_CHAR:
		pc += unicode_utf8seqlen[*pc];
		goto again;

	case RECS_CHAR2:
	case RECS_RANGE:
	case RECS_RANGE_ICASE:
		pc += unicode_utf8seqlen[*pc];
		pc += unicode_utf8seqlen[*pc];
		goto again;

	case RECS_CONTAINS: {
		byte_t len = *pc++;
		assert(len >= 1);
		do {
			pc += unicode_utf8seqlen[*pc];
		} while (--len);
		goto again;
	}

	case_RECS_ISX_MIN_to_MAX:
		goto again;

	default: __builtin_unreachable();
	}
	return pc;
}

/* Check if reg-match `a' is better than `b' */
PRIVATE ATTR_PURE WUNUSED NONNULL((1, 2)) bool
NOTHROW_NCX(CC is_regmatch_better)(re_regmatch_t const *__restrict a,
                                   re_regmatch_t const *__restrict b,
                                   uint16_t nregs) {
	uint16_t i;
	for (i = 0; i < nregs; ++i) {
		re_sregoff_t a_startoff, a_endoff;
		re_sregoff_t b_startoff, b_endoff;

		/* We prefer groups that end later. */
		a_endoff = (re_sregoff_t)a[i].rm_eo;
		b_endoff = (re_sregoff_t)b[i].rm_eo;
		if (a_endoff > b_endoff)
			return true;
		if (a_endoff < b_endoff)
			return false;

		/* We prefer groups that start earlier. */
		a_startoff = (re_sregoff_t)a[i].rm_so;
		b_startoff = (re_sregoff_t)b[i].rm_so;
		if (a_startoff < b_startoff)
			return true;
		if (a_startoff > b_startoff)
			return false;
	}

	/* The group matches are identical (but indicate `false'
	 * since that  allows our  caller  to skip  some  stuff) */
	return false;
}


/* Execute the regex interpreter.
 * NOTE: The caller is  responsible for loading  a non-empty  chunk,
 *       unless the entire input buffer is empty. iow: this function
 *       is  allowed to  assume that  the current  chunk being empty
 *       also means that the entire input buffer is empty.
 *
 * @return: -RE_NOERROR: Input was matched.
 * @return: -RE_NOMATCH: Nothing was matched
 * @return: -RE_ESPACE:  Out of memory
 * @return: -RE_ESIZE:   On-failure stack became too large. */
PRIVATE WUNUSED NONNULL((1)) re_errno_t
NOTHROW_NCX(CC libre_interp_exec)(__register struct re_interpreter *__restrict self) {
	__register byte_t opcode;
	__register byte_t const *pc;

	/* Initialize program counter. */
	{
		struct re_code const *code;
		code = self->ri_exec->rx_code;
		pc   = code->rc_code;

		/* Try to do a quick can-check via the fast-map */
		if (re_interpreter_in_chunk_cangetc(self)) {
			byte_t fmap;
			fmap = *self->ri_in_ptr;
			fmap = code->rc_fmap[fmap];
			if (fmap == 0xff) {
				/* Initial character never matches, but are we able to match epsilon? */
				if (code->rc_minmatch == 0)
					goto do_epsilon_match; /* epsilon match! */
				return -RE_NOMATCH;
			}
			pc += fmap;
		} else if (code->rc_minmatch > 0) {
			/* Input buffer is epsilon, but regex has a non-  zero
			 * minimal match length -> regex can't possibly match! */
			return -RE_NOMATCH;
		} else {
			/* Input buffer is epsilon, and we can match epsilon
			 *
			 * In  this case, simply set the start/end-offsets of
			 * all groups to the base-offset of the input buffer. */
			static_assert(sizeof(re_regmatch_t) == 2 * sizeof(re_regoff_t));
do_epsilon_match:
			if (self->ri_exec->rx_nmatch == 0) {
				/* Caller doesn't care about matches -> don't have to fill in groups properly! */
				return -RE_NOERROR;
			} else if (code->rc_flags & RE_CODE_FLAG_OPTGROUPS) {
				/* Special case: when the code contains optional groups (e.g. "(|foo(b)ar)"),
				 *               then we can't just blindly fill all groups in as matching at
				 *               offset=rx_startoff.  In the given example. group[0] needs to
				 *               have those offsets, but group[1] needs to remain UNSET!
				 * -> As such, fallthru to below and actually execute the regex code, so it
				 *    can go down the epsilon-match path  and fill in exactly those  groups
				 *    that should be filled in. */
			} else {
				/* Code doesn't have optional group; i.e. all offsets of all groups can
				 * simply be set to the start-offset */
				memsetc(self->ri_pmatch,
				        (re_regoff_t)self->ri_exec->rx_startoff,
				        code->rc_ngrps * 2, sizeof(re_regoff_t));
				return -RE_NOERROR;
			}
		}
	}

	/* Initialize the best match as not-matched-yet */
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
	self->ri_bmatch.ri_in_ptr = NULL;
#define best_match_isvalid() (self->ri_bmatch.ri_in_ptr != NULL)
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
	self->ri_bmatch.ri_in_ptr  = (byte_t *)1;
	self->ri_bmatch.ri_in_cend = (byte_t *)0;
#define best_match_isvalid() (self->ri_bmatch.ri_in_ptr <= self->ri_bmatch.ri_in_cend)
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */

	/* Helper macros */
#define DISPATCH()     goto dispatch
#ifdef HAVE_TRACE
#define ONFAIL()       do{ TRACE("ONFAIL: %d\n", __LINE__); goto onfail; }__WHILE0
#define TARGET(opcode) __IF0 { case opcode: TRACE("%#.4" PRIxSIZ ": %s\n", (size-t)((pc - 1) - self->ri_exec->rx_code->rc_code), #opcode); }
#define XTARGET(range) __IF0 {       range: TRACE("%#.4" PRIxSIZ ": %s\n", (size-t)((pc - 1) - self->ri_exec->rx_code->rc_code), #opcode); }
#else /* HAVE_TRACE */
#define ONFAIL()       goto onfail
#define TARGET(opcode) case opcode:
#define XTARGET(range) range:
#endif /* !HAVE_TRACE */
#define PUSHFAIL(pc)        do { if unlikely(!re_interpreter_pushfail(self, pc)) goto err_nomem; } __WHILE0
#define PUSHFAIL_DUMMY(pc)  do { if unlikely(!re_interpreter_pushfail_dummy(self, RE_ONFAILURE_ITEM_DUMMY_INPTR, pc)) goto err_nomem; } __WHILE0
#define PUSHFAIL_EX(in, pc) do { if unlikely(!re_interpreter_pushfail_dummy(self, in, pc)) goto err_nomem; } __WHILE0
#define getb()              (*pc++)
#define getw()              (pc += 2, delta16_get(pc - 2))

	/* The main dispatch loop */
dispatch:
	opcode = getb();
	switch (opcode) {

		TARGET(REOP_EXACT) {
			byte_t count = getb();
			assert(count >= 2);
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
			{
				size_t avail = re_interpreter_in_chunkleft(self);
				if (avail < (size_t)count)
					ONFAIL();
				if (bcmp(self->ri_in_ptr, pc, count) != 0)
					ONFAIL();
				self->ri_in_ptr += count;
				pc += count;
			}
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
			for (;;) {
				size_t avail = re_interpreter_in_chunkleft(self);
				if (avail == 0) {
					if (re_interpreter_is_eoi_at_end_of_chunk(self))
						ONFAIL();
					re_interpreter_nextchunk(self);
					avail = re_interpreter_in_chunkleft(self);
				}
				assert(avail >= 1);
				if likely(avail >= count) {
					/* Everything left to compare is in the current chunk */
					if (bcmp(self->ri_in_ptr, pc, count) != 0)
						ONFAIL();
					self->ri_in_ptr += count;
					pc += count;
					break;
				} else {
					/* Input string spans across multiple chunks */
					if (bcmp(self->ri_in_ptr, pc, avail) != 0)
						ONFAIL();
					self->ri_in_ptr += avail;
					pc += avail;
					count -= avail;
				}
			}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
			DISPATCH();
		}

		TARGET(REOP_EXACT_ASCII_ICASE) {
			byte_t count = getb();
			assert(count >= 2);
#ifdef LIBREGEX_REGEXEC_SINGLE_CHUNK
			{
				size_t avail = re_interpreter_in_chunkleft(self);
				if (avail < (size_t)count)
					ONFAIL();
				if (memcasecmp(self->ri_in_ptr, pc, count) != 0)
					ONFAIL();
				self->ri_in_ptr += count;
				pc += count;
			}
#else /* LIBREGEX_REGEXEC_SINGLE_CHUNK */
			for (;;) {
				size_t avail = re_interpreter_in_chunkleft(self);
				if (avail == 0) {
					if (re_interpreter_is_eoi_at_end_of_chunk(self))
						ONFAIL();
					re_interpreter_nextchunk(self);
					avail = re_interpreter_in_chunkleft(self);
				}
				assert(avail >= 1);
				if likely(avail >= count) {
					/* Everything left to compare is in the current chunk */
					if (memcasecmp(self->ri_in_ptr, pc, count) != 0)
						ONFAIL();
					self->ri_in_ptr += count;
					pc += count;
					break;
				} else {
					/* Input string spans across multiple chunks */
					if (memcasecmp(self->ri_in_ptr, pc, avail) != 0)
						ONFAIL();
					self->ri_in_ptr += avail;
					pc += avail;
					count -= avail;
				}
			}
#endif /* !LIBREGEX_REGEXEC_SINGLE_CHUNK */
			DISPATCH();
		}

		TARGET(REOP_EXACT_UTF8_ICASE) {
			byte_t count = getb();
			byte_t const *newpc = pc;
			assert(count >= 1);
			do {
				char32_t expected, actual;
				if (re_interpreter_is_eoi(self))
					ONFAIL();
				actual   = re_interpreter_readutf8(self);
				expected = unicode_readutf8((char const **)&newpc);
				if (actual != expected) {
					actual   = unicode_tolower(actual);
					expected = unicode_tolower(expected);
					if (actual != expected)
						ONFAIL();
				}
			} while (--count);
			pc = newpc;
			DISPATCH();
		}

#ifdef REOP_ANY
		TARGET(REOP_ANY) {
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			(void)re_interpreter_readbyte(self);
			DISPATCH();
		}
#endif /* REOP_ANY */

#ifdef REOP_ANY_UTF8
		TARGET(REOP_ANY_UTF8) {
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			(void)re_interpreter_readutf8(self);
			DISPATCH();
		}
#endif /* REOP_ANY_UTF8 */

#ifdef REOP_ANY_NOTLF
		TARGET(REOP_ANY_NOTLF) {
			byte_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readbyte(self);
			if (ascii_islf(ch))
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTLF */

#ifdef REOP_ANY_NOTLF_UTF8
		TARGET(REOP_ANY_NOTLF_UTF8) {
			char32_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
			if (unicode_islf(ch))
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTLF_UTF8 */

#ifdef REOP_ANY_NOTNUL
		TARGET(REOP_ANY_NOTNUL) {
			byte_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readbyte(self);
			if (ch == '\0')
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTNUL */

#ifdef REOP_ANY_NOTNUL_UTF8
		TARGET(REOP_ANY_NOTNUL_UTF8) {
			char32_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readbyte(self);
			if (ch == (char32_t)'\0')
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTNUL_UTF8 */

#ifdef REOP_ANY_NOTNUL_NOTLF
		TARGET(REOP_ANY_NOTNUL_NOTLF) {
			byte_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readbyte(self);
			if (ch == '\0' || ascii_islf(ch))
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTNUL_NOTLF */

#ifdef REOP_ANY_NOTNUL_NOTLF_UTF8
		TARGET(REOP_ANY_NOTNUL_NOTLF_UTF8) {
			char32_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
			if (ch == '\0' || unicode_islf(ch))
				ONFAIL();
			DISPATCH();
		}
#endif /* REOP_ANY_NOTNUL_NOTLF_UTF8 */

		TARGET(REOP_BYTE) {
			/* Followed by 1 byte that must be matched exactly */
			byte_t ch, b;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			b  = getb();
			ch = re_interpreter_readbyte(self);
			if (ch == b)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_NBYTE) {
			/* Followed by 1 byte that must not be matched exactly */
			byte_t ch, b;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			b  = getb();
			ch = re_interpreter_readbyte(self);
			if (ch != b)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_BYTE2) {
			/* Followed by 2 bytes, one of which must be matched exactly (for "[ab]" or "a" -> "[aA]" in ICASE-mode) */
			byte_t ch, b1, b2;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			b1 = getb();
			b2 = getb();
			ch = re_interpreter_readbyte(self);
			if (ch == b1 || ch == b2)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_NBYTE2) {
			/* Followed by 2 bytes, neither of which may be matched */
			byte_t ch, b1, b2;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			b1 = getb();
			b2 = getb();
			ch = re_interpreter_readbyte(self);
			if (ch != b1 && ch != b2)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_RANGE) {
			/* Followed by 2 bytes, with input having to match `ch >= pc[0] && ch <= pc[1]' */
			byte_t ch, lo, hi;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			lo = getb();
			hi = getb();
			ch = re_interpreter_readbyte(self);
			if (ch >= lo && ch <= hi)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_NRANGE) {
			/* Followed by 2 bytes, with input having to match `ch >= pc[0] && ch <= pc[1]' */
			byte_t ch, lo, hi;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			lo = getb();
			hi = getb();
			ch = re_interpreter_readbyte(self);
			if (ch < lo || ch > hi)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_CONTAINS_UTF8) {
			byte_t count = getb();
			char32_t ch;
			byte_t const *newpc;
			assert(count >= 2);
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
			newpc = pc;
			for (;;) {
				char32_t other_ch;
				other_ch = unicode_readutf8((char const **)&newpc);
				--count;
				if (ch == other_ch)
					break;
				if (!count)
					ONFAIL();
			}
			/* Consume remaining characters */
			for (; count; --count)
				newpc += unicode_utf8seqlen[(unsigned char)*newpc];
			pc = newpc;
			DISPATCH();
		}

		TARGET(REOP_NCONTAINS_UTF8) {
			byte_t count = getb();
			char32_t ch;
			byte_t const *newpc;
			assert(count >= 1);
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
			newpc = pc;
			do {
				char32_t other_ch;
				other_ch = unicode_readutf8((char const **)&newpc);
				if (ch == other_ch)
					ONFAIL();
			} while (--count);
			pc = newpc;
			DISPATCH();
		}



		/************************************************************************/
		/* BITSET OPCODES                                                       */
		/************************************************************************/
		TARGET(REOP_CS_BYTE) {
			byte_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readbyte(self);
REOP_CS_BYTE_dispatch:
			opcode = getb();
			switch (opcode) {

			case_RECS_BITSET_MIN_to_MAX_BYTE: {
				uint8_t bitset_minch = RECS_BITSET_GETBASE(opcode);
				uint8_t bitset_size  = RECS_BITSET_GETBYTES(opcode);
				byte_t bitset_rel_ch;
				if (!OVERFLOW_USUB(ch, bitset_minch, &bitset_rel_ch)) {
					unsigned int bitset_bits = bitset_size * 8;
					if (bitset_rel_ch < bitset_bits) {
						if ((pc[bitset_rel_ch / 8] & (1 << (bitset_rel_ch % 8))) != 0) {
							pc += bitset_size;
							goto REOP_CS_BYTE_onmatch;
						}
					}
				}
				pc += bitset_size;
				goto REOP_CS_BYTE_dispatch;
			}

			case RECS_DONE:
				/* Reached the end of the char-set without any match */
				ONFAIL();
				__builtin_unreachable();

			case RECS_CHAR: {
				byte_t match = getb();
				if (ch == match)
					goto REOP_CS_BYTE_onmatch;
				goto REOP_CS_BYTE_dispatch;
			}

			case RECS_CHAR2: {
				byte_t match1 = getb();
				byte_t match2 = getb();
				if (ch == match1 || ch == match2)
					goto REOP_CS_BYTE_onmatch;
				goto REOP_CS_BYTE_dispatch;
			}

			case RECS_RANGE: {
				byte_t match_lo = getb();
				byte_t match_hi = getb();
				if (ch >= match_lo && ch <= match_hi)
					goto REOP_CS_BYTE_onmatch;
				goto REOP_CS_BYTE_dispatch;
			}

			case RECS_CONTAINS: {
				byte_t len = getb();
				if (memchr(pc, ch, len) != NULL) {
					pc += len;
					goto REOP_CS_BYTE_onmatch;
				}
				pc += len;
				goto REOP_CS_BYTE_dispatch;
			}

			/* No need to handle trait opcodes (those aren't valid in byte-mode) */

			default: __builtin_unreachable();
			}
REOP_CS_BYTE_onmatch:
			pc = CS_BYTE_seek_end(pc);
			DISPATCH();
		}

		TARGET(REOP_CS_UTF8) {
			char32_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
REOP_CS_UTF8_dispatch:
			opcode = getb();
			switch (opcode) {

			case_RECS_BITSET_MIN_to_MAX_UTF8: {
				uint8_t bitset_minch = RECS_BITSET_GETBASE(opcode);
				uint8_t bitset_size  = RECS_BITSET_GETBYTES(opcode);
				byte_t bitset_rel_ch;
				if (ch < 0x80 && !OVERFLOW_USUB((uint8_t)ch, bitset_minch, &bitset_rel_ch)) {
					unsigned int bitset_bits = bitset_size * 8;
					if (bitset_rel_ch < bitset_bits) {
						if ((pc[bitset_rel_ch / 8] & (1 << (bitset_rel_ch % 8))) != 0) {
							pc += bitset_size;
							goto REOP_CS_UTF8_onmatch;
						}
					}
				}
				pc += bitset_size;
				goto REOP_CS_UTF8_dispatch;
			}

			case RECS_DONE:
				/* Reached the end of the char-set without any match */
				ONFAIL();
				__builtin_unreachable();

			case RECS_CHAR: {
				byte_t const *newpc = pc;
				char32_t match;
				match = unicode_readutf8((char const **)&newpc);
				pc    = newpc;
				if (ch == match)
					goto REOP_CS_UTF8_onmatch;
				goto REOP_CS_UTF8_dispatch;
			}

			case RECS_CHAR2: {
				byte_t const *newpc = pc;
				char32_t match1, match2;
				match1 = unicode_readutf8((char const **)&newpc);
				match2 = unicode_readutf8((char const **)&newpc);
				pc     = newpc;
				if (ch == match1 || ch == match2)
					goto REOP_CS_UTF8_onmatch;
				goto REOP_CS_UTF8_dispatch;
			}

			case RECS_RANGE: {
				byte_t const *newpc = pc;
				char32_t match_lo, match_hi;
				match_lo = unicode_readutf8((char const **)&newpc);
				match_hi = unicode_readutf8((char const **)&newpc);
				pc       = newpc;
				if (ch >= match_lo && ch <= match_hi)
					goto REOP_CS_UTF8_onmatch;
				goto REOP_CS_UTF8_dispatch;
			}

			case RECS_RANGE_ICASE: {
				byte_t const *newpc = pc;
				char32_t match_lo, match_hi, lower_ch;
				match_lo = unicode_readutf8((char const **)&newpc);
				match_hi = unicode_readutf8((char const **)&newpc);
				pc       = newpc;
				lower_ch = unicode_tolower(ch);
				if (lower_ch >= match_lo && lower_ch <= match_hi)
					goto REOP_CS_UTF8_onmatch;
				goto REOP_CS_UTF8_dispatch;
			}

			case RECS_CONTAINS: {
				byte_t len = getb();
				byte_t const *newpc = pc;
				assert(len >= 3);
				do {
					char32_t expected_ch;
					--len;
					expected_ch = unicode_readutf8((char const **)&newpc);
					if (ch == expected_ch) {
						while (len) {
							newpc += unicode_utf8seqlen[*newpc];
							--len;
						}
						pc = newpc;
						goto REOP_CS_UTF8_onmatch;
					}
				} while (len);
				pc = newpc;
				goto REOP_CS_UTF8_dispatch;
			}

			case_RECS_ISX_MIN_to_MAX: {
				uint16_t trait    = libre_unicode_traits[opcode - RECS_ISX_MIN];
				uint16_t ch_flags = __unicode_descriptor(ch)->__ut_flags;
				uint16_t ch_mask  = ch_flags & trait;
				if (ch_mask != 0)
					goto REOP_CS_UTF8_onmatch;
				goto REOP_CS_UTF8_dispatch;
			}

			default: __builtin_unreachable();
			}
REOP_CS_UTF8_onmatch:
			pc = CS_UTF8_seek_end(pc);
			DISPATCH();
		}

		TARGET(REOP_NCS_UTF8) {
			char32_t ch;
			if (re_interpreter_is_eoi(self))
				ONFAIL();
			ch = re_interpreter_readutf8(self);
REOP_NCS_UTF8_dispatch:
			opcode = getb();
			switch (opcode) {

			case_RECS_BITSET_MIN_to_MAX_UTF8: {
				uint8_t bitset_minch = RECS_BITSET_GETBASE(opcode);
				uint8_t bitset_size  = RECS_BITSET_GETBYTES(opcode);
				byte_t bitset_rel_ch;
				if (ch < 0x80 && !OVERFLOW_USUB((uint8_t)ch, bitset_minch, &bitset_rel_ch)) {
					unsigned int bitset_bits = bitset_size * 8;
					if (bitset_rel_ch < bitset_bits) {
						if ((pc[bitset_rel_ch / 8] & (1 << (bitset_rel_ch % 8))) != 0)
							ONFAIL();
					}
				}
				pc += bitset_size;
				goto REOP_NCS_UTF8_dispatch;
			}

			case RECS_DONE:
				/* Reached the end of the char-set without a miss-match */
				DISPATCH();

			case RECS_CHAR: {
				byte_t const *newpc = pc;
				char32_t match;
				match = unicode_readutf8((char const **)&newpc);
				pc    = newpc;
				if (ch == match)
					ONFAIL();
				goto REOP_NCS_UTF8_dispatch;
			}

			case RECS_CHAR2: {
				byte_t const *newpc = pc;
				char32_t match1, match2;
				match1 = unicode_readutf8((char const **)&newpc);
				match2 = unicode_readutf8((char const **)&newpc);
				pc     = newpc;
				if (ch == match1 || ch == match2)
					ONFAIL();
				goto REOP_NCS_UTF8_dispatch;
			}

			case RECS_RANGE: {
				byte_t const *newpc = pc;
				char32_t match_lo, match_hi;
				match_lo = unicode_readutf8((char const **)&newpc);
				match_hi = unicode_readutf8((char const **)&newpc);
				pc       = newpc;
				if (ch >= match_lo && ch <= match_hi)
					ONFAIL();
				goto REOP_NCS_UTF8_dispatch;
			}

			case RECS_RANGE_ICASE: {
				byte_t const *newpc = pc;
				char32_t match_lo, match_hi, lower_ch;
				match_lo = unicode_readutf8((char const **)&newpc);
				match_hi = unicode_readutf8((char const **)&newpc);
				pc       = newpc;
				lower_ch = unicode_tolower(ch);
				if (lower_ch >= match_lo && lower_ch <= match_hi)
					ONFAIL();
				goto REOP_NCS_UTF8_dispatch;
			}

			case RECS_CONTAINS: {
				byte_t len = getb();
				byte_t const *newpc = pc;
				assert(len >= 3);
				do {
					char32_t expected_ch;
					--len;
					expected_ch = unicode_readutf8((char const **)&newpc);
					if (ch == expected_ch)
						ONFAIL();
				} while (len);
				pc = newpc;
				goto REOP_NCS_UTF8_dispatch;
			}

			case_RECS_ISX_MIN_to_MAX: {
				uint16_t trait    = libre_unicode_traits[opcode - RECS_ISX_MIN];
				uint16_t ch_flags = __unicode_descriptor(ch)->__ut_flags;
				uint16_t ch_mask  = ch_flags & trait;
				if (ch_mask != 0)
					ONFAIL();
				goto REOP_NCS_UTF8_dispatch;
			}

			default: __builtin_unreachable();
			}
			__builtin_unreachable();
		}



		/************************************************************************/
		/* Group repetition                                                     */
		/************************************************************************/
		TARGET(REOP_GROUP_MATCH) {
			byte_t gid = getb();
			re_regmatch_t match;
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			match = self->ri_pmatch[gid];
			if (match.rm_so == RE_REGOFF_UNSET ||
			    match.rm_eo == RE_REGOFF_UNSET)
				ONFAIL();
			assertf(self->ri_pmatch[gid].rm_so <= self->ri_pmatch[gid].rm_eo,
			        "self->ri_pmatch[%1$" PRIu8 "].rm_so = %2$" PRIuSIZ "\n"
			        "self->ri_pmatch[%1$" PRIu8 "].rm_eo = %3$" PRIuSIZ,
			        gid, (size_t)match.rm_so, (size_t)match.rm_eo);
			if (match.rm_so < match.rm_eo) {
				if (!re_interpreter_consume_repeat(self, match.rm_so,
				                                   match.rm_eo - match.rm_so))
					ONFAIL();
			}
			DISPATCH();
		}

		XTARGET(case_REOP_GROUP_MATCH_JMIN_to_JMAX) {
			byte_t gid = getb();
			re_regmatch_t match;
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			match = self->ri_pmatch[gid];
			if (match.rm_so == RE_REGOFF_UNSET ||
			    match.rm_eo == RE_REGOFF_UNSET)
				ONFAIL();
			assertf(self->ri_pmatch[gid].rm_so <= self->ri_pmatch[gid].rm_eo,
			        "self->ri_pmatch[%1$" PRIu8 "].rm_so = %2$" PRIuSIZ "\n"
			        "self->ri_pmatch[%1$" PRIu8 "].rm_eo = %3$" PRIuSIZ,
			        gid, (size_t)match.rm_so, (size_t)match.rm_eo);
			if (match.rm_so < match.rm_eo) {
				if (!re_interpreter_consume_repeat(self, match.rm_so,
				                                   match.rm_eo - match.rm_so))
					ONFAIL();
			} else {
				/* Empty group -> do a custom jump-ahead */
				pc += REOP_GROUP_MATCH_Joff(opcode);
			}
			DISPATCH();
		}



		/************************************************************************/
		/* Opcodes for asserting the current position in input (these don't consume anything) */
		/************************************************************************/
		TARGET(REOP_AT_SOI) {
			/* Start-of-input */
			if (!re_interpreter_is_soi(self))
				ONFAIL();
			DISPATCH();
		}

		TARGET(REOP_AT_EOI) {
			/* End-of-input */
			if (!re_interpreter_is_eoi(self))
				ONFAIL();
			DISPATCH();
		}

		TARGET(REOP_AT_SOL) {
			/* Start-of-line (following a line-feed, or `REOP_AT_SOI' unless `RE_EXEC_NOTBOL' was set) */
			if (re_interpreter_is_soi(self)) {
				DISPATCH();
			} else {
				byte_t prevbyte;
				prevbyte = re_interpreter_prevbyte(self);
				if (ascii_islf(prevbyte))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_SOL_UTF8) {
			/* Start-of-line (following a line-feed, or `REOP_AT_SOI' unless `RE_EXEC_NOTBOL' was set) */
			if (re_interpreter_is_soi(self)) {
				DISPATCH();
			} else {
				char32_t prevchar;
				prevchar = re_interpreter_prevutf8(self);
				if (unicode_islf(prevchar))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_EOL) {
			/* End-of-line (preceding a line-feed, or `REOP_AT_EOI' unless `RE_EXEC_NOTEOL' was set) */
			if (re_interpreter_is_eoiX(self)) {
				DISPATCH();
			} else {
				byte_t nextbyte;
				nextbyte = re_interpreter_nextbyte(self);
				if (ascii_islf(nextbyte))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_EOL_UTF8) {
			/* End-of-line (preceding a line-feed, or `REOP_AT_EOI' unless `RE_EXEC_NOTEOL' was set) */
			if (re_interpreter_is_eoiX(self)) {
				DISPATCH();
			} else {
				char32_t nextchar;
				nextchar = re_interpreter_nextutf8(self);
				if (unicode_islf(nextchar))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_SOXL) {
			/* Start-of-line (following a line-feed, or `REOP_AT_SOI' unless `RE_EXEC_NOTBOL' was set) */
			if (re_interpreter_is_soi(self)) {
				if (!(self->ri_exec->rx_eflags & RE_EXEC_NOTBOL))
					DISPATCH();
			} else {
				byte_t prevbyte;
				prevbyte = re_interpreter_prevbyte(self);
				if (ascii_islf(prevbyte))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_SOXL_UTF8) {
			/* Start-of-line (following a line-feed, or `REOP_AT_SOI' unless `RE_EXEC_NOTBOL' was set) */
			if (re_interpreter_is_soi(self)) {
				if (!(self->ri_exec->rx_eflags & RE_EXEC_NOTBOL))
					DISPATCH();
			} else {
				char32_t prevchar;
				prevchar = re_interpreter_prevutf8(self);
				if (unicode_islf(prevchar))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_EOXL) {
			/* End-of-line (preceding a line-feed, or `REOP_AT_EOI' unless `RE_EXEC_NOTEOL' was set) */
			if (re_interpreter_is_eoiX(self)) {
				if (!(self->ri_exec->rx_eflags & RE_EXEC_NOTEOL))
					DISPATCH();
			} else {
				byte_t nextbyte;
				nextbyte = re_interpreter_nextbyte(self);
				if (ascii_islf(nextbyte))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_EOXL_UTF8) {
			/* End-of-line (preceding a line-feed, or `REOP_AT_EOI' unless `RE_EXEC_NOTEOL' was set) */
			if (re_interpreter_is_eoiX(self)) {
				if (!(self->ri_exec->rx_eflags & RE_EXEC_NOTEOL))
					DISPATCH();
			} else {
				char32_t nextchar;
				nextchar = re_interpreter_nextutf8(self);
				if (unicode_islf(nextchar))
					DISPATCH();
			}
			ONFAIL();
		}

		TARGET(REOP_AT_WOB)     /* WOrdBoundary (preceding and next character have non-equal `issymcont(ch)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_WOB_NOT) /* NOT WOrdBoundary (preceding and next character have equal `issymcont(ch)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_SOW)     /* StartOfWord (preceding and next character are `!issymcont(lhs) && issymcont(rhs)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_EOW)     /* EndOfWord (preceding and next character are `issymcont(lhs) && !issymcont(rhs)'; OOB counts as `issymcont == false') */
		{
			bool previs = re_interpreter_is_soi(self) ? false : !!issymcont(re_interpreter_prevbyte(self));
			bool nextis = re_interpreter_is_eoiX(self) ? false : !!issymcont(re_interpreter_nextbyte(self));
			bool ismatch;
			switch (opcode) {
			case REOP_AT_WOB:
				ismatch = previs != nextis;
				break;
			case REOP_AT_WOB_NOT:
				ismatch = previs == nextis;
				break;
			case REOP_AT_SOW:
				ismatch = !previs && nextis;
				break;
			case REOP_AT_EOW:
				ismatch = previs && !nextis;
				break;
			default: __builtin_unreachable();
			}
			if (ismatch)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_AT_WOB_UTF8)     /* WOrdBoundary (preceding and next character have non-equal `issymcont(ch)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_WOB_UTF8_NOT) /* NOT WOrdBoundary (preceding and next character have equal `issymcont(ch)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_SOW_UTF8)     /* StartOfWord (preceding and next character are `!issymcont(lhs) && issymcont(rhs)'; OOB counts as `issymcont == false') */
		TARGET(REOP_AT_EOW_UTF8)     /* EndOfWord (preceding and next character are `issymcont(lhs) && !issymcont(rhs)'; OOB counts as `issymcont == false') */
		{
			bool previs = re_interpreter_is_soi(self) ? false : !!unicode_issymcont(re_interpreter_prevutf8(self));
			bool nextis = re_interpreter_is_eoiX(self) ? false : !!unicode_issymcont(re_interpreter_nextutf8(self));
			bool ismatch;
			switch (opcode) {
			case REOP_AT_WOB_UTF8:
				ismatch = previs != nextis;
				break;
			case REOP_AT_WOB_UTF8_NOT:
				ismatch = previs == nextis;
				break;
			case REOP_AT_SOW_UTF8:
				ismatch = !previs && nextis;
				break;
			case REOP_AT_EOW_UTF8:
				ismatch = previs && !nextis;
				break;
			default: __builtin_unreachable();
			}
			if (ismatch)
				DISPATCH();
			ONFAIL();
		}

		TARGET(REOP_AT_SOS_UTF8) {
			/* StartOfSymbol (preceding and next character are `!issymcont(lhs) && issymstrt(rhs)'; OOB counts as `issymcont[/strt] == false') */
			bool previs = re_interpreter_is_soi(self) ? false : !!unicode_issymcont(re_interpreter_prevutf8(self));
			bool nextis = re_interpreter_is_eoiX(self) ? false : !!unicode_issymstrt(re_interpreter_nextutf8(self));
			if (!previs && nextis)
				DISPATCH();
			ONFAIL();
		}




		/************************************************************************/
		/* Opcodes for expression logic and processing.                         */
		/************************************************************************/
		TARGET(REOP_GROUP_START) {
			byte_t gid = getb();
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			if (self->ri_onfailc && /* No need to make a backup if it can't be restored */
			    (self->ri_pmatch[gid].rm_so != re_interpreter_in_curoffset(self))) {
				/* Must push (or override an old) on-fail item to restore old group start-offset */
				size_t i = self->ri_onfailc;
				do {
					byte_t const *oldin;
					--i;
					oldin = self->ri_onfailv[i].rof_in;
					if (RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(oldin)) {
						/* Check if we can override this on-fail item. */
						if (RE_ONFAILURE_ITEM_GROUP_RESTORE_ISSTART(oldin) &&
						    RE_ONFAILURE_ITEM_GROUP_RESTORE_GETGID(oldin) == gid) {
							self->ri_onfailv[i].rof_pc = (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_so;
							goto do_set_group_so_and_dispatch;
						}
					} else {
						/* Must actually push an new on-fail item. */
						break;
					}
				} while (i);
				/* Push a new on-fail item. */
				PUSHFAIL_EX(RE_ONFAILURE_ITEM_GROUP_RESTORE_ENCODE(1, gid),
				            (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_so);
			}
do_set_group_so_and_dispatch:
			/* Set start-of-group offset */
			self->ri_pmatch[gid].rm_so = re_interpreter_in_curoffset(self);
			DISPATCH();
		}

		TARGET(REOP_GROUP_END) {
			byte_t gid = getb();
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			if (self->ri_onfailc && /* No need to make a backup if it can't be restored */
			    (self->ri_pmatch[gid].rm_eo != re_interpreter_in_curoffset(self))) {
				/* Must push (or override an old) on-fail item to restore old group end-offset */
				size_t i = self->ri_onfailc;
				do {
					byte_t const *oldin;
					--i;
					oldin = self->ri_onfailv[i].rof_in;
					if (RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(oldin)) {
						/* Check if we can override this on-fail item. */
						if (!RE_ONFAILURE_ITEM_GROUP_RESTORE_ISSTART(oldin) &&
						    RE_ONFAILURE_ITEM_GROUP_RESTORE_GETGID(oldin) == gid) {
							self->ri_onfailv[i].rof_pc = (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_eo;
							goto do_set_group_eo_and_dispatch;
						}
					} else {
						/* Must actually push an new on-fail item. */
						break;
					}
				} while (i);
				/* Push a new on-fail item. */
				PUSHFAIL_EX(RE_ONFAILURE_ITEM_GROUP_RESTORE_ENCODE(0, gid),
				            (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_eo);
			}
do_set_group_eo_and_dispatch:
			/* Set end-of-group offset */
			self->ri_pmatch[gid].rm_eo = re_interpreter_in_curoffset(self);
			assertf(self->ri_pmatch[gid].rm_so <= self->ri_pmatch[gid].rm_eo,
			        "self->ri_pmatch[%1$" PRIu8 "].rm_so = %2$" PRIuSIZ "\n"
			        "self->ri_pmatch[%1$" PRIu8 "].rm_eo = %3$" PRIuSIZ,
			        gid,
			        (size_t)self->ri_pmatch[gid].rm_so,
			        (size_t)self->ri_pmatch[gid].rm_eo);
			DISPATCH();
		}

		XTARGET(case_REOP_GROUP_END_JMIN_to_JMAX) {
			byte_t gid = getb();
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			if (self->ri_onfailc && /* No need to make a backup if it can't be restored */
			    (self->ri_pmatch[gid].rm_eo != re_interpreter_in_curoffset(self))) {
				/* Must push (or override an old) on-fail item to restore old group end-offset */
				size_t i = self->ri_onfailc;
				do {
					byte_t const *oldin;
					--i;
					oldin = self->ri_onfailv[i].rof_in;
					if (RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(oldin)) {
						/* Check if we can override this on-fail item. */
						if (!RE_ONFAILURE_ITEM_GROUP_RESTORE_ISSTART(oldin) &&
						    RE_ONFAILURE_ITEM_GROUP_RESTORE_GETGID(oldin) == gid) {
							self->ri_onfailv[i].rof_pc = (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_eo;
							goto do_set_group_eo_and_dispatch_j;
						}
					} else {
						/* Must actually push an new on-fail item. */
						break;
					}
				} while (i);
				/* Push a new on-fail item. */
				PUSHFAIL_EX(RE_ONFAILURE_ITEM_GROUP_RESTORE_ENCODE(0, gid),
				            (byte_t const *)(uintptr_t)self->ri_pmatch[gid].rm_eo);
			}
do_set_group_eo_and_dispatch_j:
			/* Set end-of-group offset */
			self->ri_pmatch[gid].rm_eo = re_interpreter_in_curoffset(self);
			assertf(self->ri_pmatch[gid].rm_so <= self->ri_pmatch[gid].rm_eo,
			        "self->ri_pmatch[%1$" PRIu8 "].rm_so = %2$" PRIuSIZ "\n"
			        "self->ri_pmatch[%1$" PRIu8 "].rm_eo = %3$" PRIuSIZ,
			        gid,
			        (size_t)self->ri_pmatch[gid].rm_so,
			        (size_t)self->ri_pmatch[gid].rm_eo);
			if (self->ri_pmatch[gid].rm_so >= self->ri_pmatch[gid].rm_eo) {
				/* Group matched epsilon -> must skip ahead a little bit */
				pc += REOP_GROUP_END_Joff(opcode);
			}
			DISPATCH();
		}

		TARGET(REOP_POP_ONFAIL) {
			if (self->ri_onfailc > 0) { /* Can be `0' because of the fmap */
				do {
					--self->ri_onfailc;
				} while (self->ri_onfailc && /* vvv keep popping until the stack becomes empty,
				                              *     of we removed a non-group-restore  element. */
				         RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(self->ri_onfailv[self->ri_onfailc].rof_in));
			}
			DISPATCH();
		}

		TARGET(REOP_POP_ONFAIL_AT) {
			int16_t delta = getw();
			byte_t const *target_pc;
			target_pc = pc + delta;
			while (self->ri_onfailc > 0) { /* pc might not exist because of the fmap */
				--self->ri_onfailc;
				if (self->ri_onfailv[self->ri_onfailc].rof_pc == target_pc &&
				    !RE_ONFAILURE_ITEM_GROUP_RESTORE_CHECK(self->ri_onfailv[self->ri_onfailc].rof_in))
					break;
			}
			DISPATCH();
		}

		TARGET(REOP_JMP_ONFAIL) {
			int16_t delta = getw();
			PUSHFAIL(pc + delta);
			DISPATCH();
		}

		TARGET(REOP_JMP_ONFAIL_DUMMY_AT) {
			int16_t delta = getw();
			PUSHFAIL_DUMMY(pc + delta);
			DISPATCH();
		}

		TARGET(REOP_JMP_ONFAIL_DUMMY) {
			PUSHFAIL_DUMMY(NULL);
			DISPATCH();
		}

		TARGET(REOP_JMP) {
			int16_t delta = getw();
			pc += delta;
			DISPATCH();
		}

		TARGET(REOP_JMP_AND_RETURN_ONFAIL) {
			int16_t delta = getw();
			PUSHFAIL(pc);
			pc += delta;
			DISPATCH();
		}

		TARGET(REOP_DEC_JMP) {
			byte_t varid = getb();
			int16_t delta = getw();
			assert(varid < self->ri_exec->rx_code->rc_nvars);
			if (self->ri_vars[varid] != 0) {
				--self->ri_vars[varid];
				pc += delta;
				DISPATCH();
			}
			DISPATCH();
		}

		TARGET(REOP_DEC_JMP_AND_RETURN_ONFAIL) {
			byte_t varid = getb();
			int16_t delta = getw();
			assert(varid < self->ri_exec->rx_code->rc_nvars);
			if (self->ri_vars[varid] != 0) {
				--self->ri_vars[varid];
				PUSHFAIL(pc);
				pc += delta;
				DISPATCH();
			}
			DISPATCH();
		}

		TARGET(REOP_SETVAR) {
			byte_t varid = getb();
			byte_t value = getb();
			assert(varid < self->ri_exec->rx_code->rc_nvars);
			/* Assign value to variable */
			self->ri_vars[varid] = value;
			DISPATCH();
		}

		TARGET(REOP_NOP) {
			DISPATCH();
		}

		TARGET(REOP_MATCHED) {
			/* Compare with a previous match. */
			if (self->ri_onfailc != 0) {
				/* Check if our current match is the best it can get. */
				if (re_interpreter_is_eoi(self)) {
					/* No need to keep going! -- It can't get any better than this.
					 *
					 * BUT: if the caller  also wants  group matches, we  have to  find
					 *      the best one of those, also (so no early exit in that case) */
					if (self->ri_exec->rx_nmatch == 0)
						return -RE_NOERROR;
				}

				/* Still have to roll back in order test more code-paths
				 * -> In  this case,  check if  the current  match is better
				 *    than the previous best match, and replace the previous
				 *    one if the new one is better. */
				if (!best_match_isvalid() ||
				    ((re_interpreter_in_curoffset_or_ptr(self) > re_interpreter_in_curoffset_or_ptr(&self->ri_bmatch)) ||
				     (re_interpreter_in_curoffset_or_ptr(self) == re_interpreter_in_curoffset_or_ptr(&self->ri_bmatch) &&
				      (self->ri_exec->rx_nmatch && is_regmatch_better(self->ri_pmatch, self->ri_bmatch_g,
				                                                      self->ri_exec->rx_code->rc_ngrps))))) {
					struct re_exec const *exec;
					/* Check if also have to  save the current state of  group-matches
					 * This is only necessary if the caller wants us to produce group-
					 * range match offsets. */
					exec = self->ri_exec;
					if (exec->rx_nmatch) {
						if (!best_match_isvalid()) {
							self->ri_bmatch_g = (re_regmatch_t *)alloca(exec->rx_code->rc_ngrps *
							                                            sizeof(re_regmatch_t));
						}
						memcpyc(self->ri_bmatch_g, self->ri_pmatch,
						        exec->rx_code->rc_ngrps,
						        sizeof(re_regmatch_t));
					}
					self->ri_bmatch = self->ri_in;
				}
				ONFAIL();
			}

			/* No more on-fail branches
			 * -> check  if the current match is better than the best. If
			 *    it isn't, then restore the best match before returning. */
			if (best_match_isvalid() &&
			    ((re_interpreter_in_curoffset_or_ptr(&self->ri_bmatch) > re_interpreter_in_curoffset_or_ptr(self)) ||
			     (re_interpreter_in_curoffset_or_ptr(&self->ri_bmatch) == re_interpreter_in_curoffset_or_ptr(self) &&
			      (self->ri_exec->rx_nmatch && is_regmatch_better(self->ri_bmatch_g, self->ri_pmatch,
			                                                      self->ri_exec->rx_code->rc_ngrps))))) {
return_best_match:
				self->ri_in = self->ri_bmatch;
				if (self->ri_exec->rx_nmatch) {
					/* Must also restore the current state of group-matches */
					memcpyc(self->ri_pmatch, self->ri_bmatch_g,
					        self->ri_exec->rx_code->rc_ngrps,
					        sizeof(re_regmatch_t));
				}
			}
			/* Fallthru to the PERFECT_MATCH opcode */
			return -RE_NOERROR;
		}

		TARGET(REOP_MATCHED_PERFECT) {
			/* Just indicate success for the current match! */
			return -RE_NOERROR;
		}

	default:
		__builtin_unreachable();
		break;
	}
	__builtin_unreachable();
	{
		struct re_onfailure_item *item;
onfail:
		if (self->ri_onfailc <= 0) {
			/* If there was a match, then return it. */
			if (best_match_isvalid())
				goto return_best_match;
			if (self->ri_flags & RE_INTERPRETER_F_RSGRPS) {
				/* Regular match fail while doing a search -> must reset groups. */
				memsetc(self->ri_pmatch, RE_REGOFF_UNSET,
				        self->ri_exec->rx_code->rc_ngrps * 2,
				        sizeof(re_regoff_t));
			}
			return -RE_NOMATCH;
		}
		item = &self->ri_onfailv[--self->ri_onfailc];
		/* Check for special on-fail stack items. */
		if (RE_ONFAILURE_ITEM_SPECIAL_CHECK(item->rof_in)) {
			re_regoff_t regoff;
			uint8_t gid;
			if (item->rof_in == RE_ONFAILURE_ITEM_DUMMY_INPTR)
				goto onfail; /* Skip dummy on-fail stack element. */

			/* Restore group start/end offset. */
			gid = RE_ONFAILURE_ITEM_GROUP_RESTORE_GETGID(item->rof_in);
			assert(gid < self->ri_exec->rx_code->rc_ngrps);
			regoff = (re_regoff_t)(uintptr_t)item->rof_pc;
			if (RE_ONFAILURE_ITEM_GROUP_RESTORE_ISSTART(item->rof_in)) {
				TRACE("%d: ri_pmatch[%" PRIu8 "].rm_so = %d\n", __LINE__, gid, (int)(re_sregoff_t)regoff);
				self->ri_pmatch[gid].rm_so = regoff;
			} else {
				TRACE("%d: ri_pmatch[%" PRIu8 "].rm_eo = %d\n", __LINE__, gid, (int)(re_sregoff_t)regoff);
				self->ri_pmatch[gid].rm_eo = regoff;
			}
			goto onfail;
		}
		pc = item->rof_pc;
		re_interpreter_setinptr(self, item->rof_in);
		DISPATCH();
	}
	__builtin_unreachable();
err_nomem:
	/* Check for special case: on-fail stack got too large. */
	if (self->ri_onfailc >= self->ri_onfaila &&
	    self->ri_onfailc > 0 &&
	    self->ri_onfaila >= get_re_max_failures())
		return -RE_ESIZE;
	return -RE_ESPACE;
#undef PUSHFAIL_EX
#undef PUSHFAIL_DUMMY
#undef PUSHFAIL
#undef XTARGET
#undef TARGET
#undef ONFAIL
#undef DISPATCH
#undef getw
#undef getb
}


/* Execute a regular expression.
 * @return: >= 0:        The # of bytes starting at `exec->rx_startoff' that got matched.
 * @return: -RE_NOMATCH: Nothing was matched
 * @return: -RE_ESPACE:  Out of memory
 * @return: -RE_ESIZE:   On-failure stack became too large. */
INTERN WUNUSED NONNULL((1)) ssize_t
NOTHROW_NCX(CC libre_exec_match)(struct re_exec const *__restrict exec) {
	ssize_t result;
	re_errno_t error;
	struct re_interpreter *interp;

	/* Quick check: is the given buffer large enough to ever match the pattern? */
	{
		size_t total_left;
		if (OVERFLOW_USUB(exec->rx_endoff, exec->rx_startoff, &total_left))
			total_left = 0;
		if (exec->rx_code->rc_minmatch > total_left)
			return -RE_NOMATCH; /* Buffer is to small to ever match */
	}

	/* Setup */
	interp = re_interpreter_alloc(exec->rx_code->rc_nvars);
	error  = re_interpreter_init(interp, exec);
	if unlikely(error != 0)
		goto err;
	re_interpreter_init_match(interp, exec, false);

	/* Execute */
	result = libre_interp_exec(interp);
	if (result == -RE_NOERROR) {
		result = re_interpreter_in_curoffset(interp) - exec->rx_startoff;
		re_interpreter_copy_match(interp);
	}
	re_interpreter_fini(interp);
	return result;
err:
	return -error;
}


/* Similar to `re_exec_match', try to match a pattern against the given input buffer. Do this
 * with increasing offsets for the first `search_range' bytes, meaning at most `search_range'
 * regex matches will be performed.
 * @param: search_range: One plus the max starting  byte offset (from `exec->rx_startoff')  to
 *                       check. Too great values for `search_range' are automatically clamped.
 * @param: p_match_size: When non-NULL, set to the # of bytes that were actually matched.
 *                       This would have  been the return  value of  `re_exec_match(3R)'.
 * @return: >= 0:        The offset where the matched area starts (in `[exec->rx_startoff, exec->rx_startoff + search_range)').
 * @return: -RE_NOMATCH: Nothing was matched
 * @return: -RE_ESPACE:  Out of memory
 * @return: -RE_ESIZE:   On-failure stack became too large. */
INTERN WUNUSED NONNULL((1)) ssize_t
NOTHROW_NCX(CC libre_exec_search)(struct re_exec const *__restrict exec,
                                  size_t search_range, size_t *p_match_size) {
	ssize_t result;
	re_errno_t error;
	struct re_interpreter *interp;
	struct re_interpreter_inptr used_inptr;
	size_t match_offset, total_left;
	if (OVERFLOW_USUB(exec->rx_endoff, exec->rx_startoff, &total_left))
		total_left = 0;
	if (OVERFLOW_USUB(total_left, exec->rx_code->rc_minmatch, &total_left))
		return -RE_NOMATCH; /* Buffer is to small to ever match */

	/* Clamp the max possible search area */
	if (search_range > total_left + 1) /* +1, so the last search is still performed (e.g. "x".refind("[[:lower:]]")) */
		search_range = total_left + 1;
	if unlikely(!search_range)
		return -RE_NOMATCH; /* Not supposed to do any searches? -- OK then... */

	/* Setup */
	interp = re_interpreter_alloc(exec->rx_code->rc_nvars);
	error  = re_interpreter_init(interp, exec);
	if unlikely(error != 0)
		goto err;
	re_interpreter_init_match(interp, exec, true);

	/* Do the search-loop */
	used_inptr   = interp->ri_in;
	match_offset = exec->rx_startoff;
	for (;;) {
		result = libre_interp_exec(interp);
		if (result != -RE_NOMATCH) {
			/* Set success result values if we didn't get here due to an error. */
			if likely(result == -RE_NOERROR) {
				if (p_match_size != NULL)
					*p_match_size = re_interpreter_in_curoffset(interp) - match_offset;
				result = (ssize_t)match_offset;
				re_interpreter_copy_match(interp);
			}
			break;
		}
		--search_range;
		if (search_range == 0)
			break;
		++match_offset;
		re_interpreter_inptr_in_advance1(&used_inptr);
		interp->ri_in = used_inptr;
	}

	/* Cleanup */
	re_interpreter_fini(interp);
	return result;
err:
	return -error;
}

/* Similar to `re_exec_search(3)',  but never returns  epsilon.
 * Instead, keep on searching if epsilon happens to be matched. */
INTERN WUNUSED NONNULL((1)) ssize_t
NOTHROW_NCX(CC libre_exec_search_noepsilon)(struct re_exec const *__restrict exec,
                                            size_t search_range, size_t *p_match_size) {
	ssize_t result;
	re_errno_t error;
	struct re_interpreter *interp;
	struct re_interpreter_inptr used_inptr;
	size_t match_offset, total_left;
	if (OVERFLOW_USUB(exec->rx_endoff, exec->rx_startoff, &total_left))
		total_left = 0;
	if (OVERFLOW_USUB(total_left, exec->rx_code->rc_minmatch, &total_left))
		return -RE_NOMATCH; /* Buffer is to small to ever match */

	/* Clamp the max possible search area */
	if (search_range > total_left + 1) /* +1, so the last search is still performed (e.g. "x".refind("[[:lower:]]")) */
		search_range = total_left + 1;
	if unlikely(!search_range)
		return -RE_NOMATCH; /* Not supposed to do any searches? -- OK then... */

	/* Setup */
	interp = re_interpreter_alloc(exec->rx_code->rc_nvars);
	error  = re_interpreter_init(interp, exec);
	if unlikely(error != 0)
		goto err;
	re_interpreter_init_match(interp, exec, true);

	/* Do the search-loop */
	used_inptr   = interp->ri_in;
	match_offset = exec->rx_startoff;
	for (;;) {
		result = libre_interp_exec(interp);
		if (result != -RE_NOMATCH) {
			/* Set success result values if we didn't get here due to an error. */
			if likely(result == -RE_NOERROR) {
				size_t match_size;
				match_size = re_interpreter_in_curoffset(interp) - match_offset;
				if (match_size == 0) {
					result = -RE_NOMATCH;
					goto advance_one;
				}
				if (p_match_size != NULL)
					*p_match_size = match_size;
				result = (ssize_t)match_offset;
				re_interpreter_copy_match(interp);
			}
			break;
		}
advance_one:
		--search_range;
		if (search_range == 0)
			break;
		++match_offset;
		re_interpreter_inptr_in_advance1(&used_inptr);
		interp->ri_in = used_inptr;
	}

	/* Cleanup */
	re_interpreter_fini(interp);
	return result;
err:
	return -error;
}

/* Same as `re_exec_search(3R)', but perform searching with starting
 * offsets  in   `[exec->rx_endoff - search_range, exec->rx_endoff)'
 * Too great values  for `search_range'  are automatically  clamped.
 * The  return value will thus be the greatest byte-offset where the
 * given pattern matches that is still within that range. */
INTERN WUNUSED NONNULL((1)) ssize_t
NOTHROW_NCX(CC libre_exec_rsearch)(struct re_exec const *__restrict exec,
                                   size_t search_range, size_t *p_match_size) {
	ssize_t result;
	re_errno_t error;
	struct re_interpreter *interp;
	struct re_interpreter_inptr used_inptr;
	size_t match_offset, total_left;
	if (OVERFLOW_USUB(exec->rx_endoff, exec->rx_startoff, &total_left))
		total_left = 0;
	if (OVERFLOW_USUB(total_left, exec->rx_code->rc_minmatch, &total_left))
		return -RE_NOMATCH; /* Buffer is to small to ever match */

	/* Clamp the max possible search area */
	if (search_range > total_left + 1) /* +1, so the last search is still performed (e.g. "x".refind("[[:lower:]]")) */
		search_range = total_left + 1;
	if unlikely(!search_range)
		return -RE_NOMATCH; /* Not supposed to do any searches? -- OK then... */

	/* Setup */
	interp = re_interpreter_alloc(exec->rx_code->rc_nvars);
	error  = re_interpreter_init(interp, exec);
	if unlikely(error != 0)
		goto err;
	re_interpreter_init_match(interp, exec, true);

	/* Do the search-loop */
	re_interpreter_inptr_advance(&interp->ri_in, total_left);
	used_inptr   = interp->ri_in;
	match_offset = exec->rx_startoff + total_left;
	for (;;) {
		result = libre_interp_exec(interp);
		if (result != -RE_NOMATCH) {
			/* Set success result values if we didn't get here due to an error. */
			if likely(result == -RE_NOERROR) {
				if (p_match_size != NULL)
					*p_match_size = re_interpreter_in_curoffset(interp) - match_offset;
				result = (ssize_t)match_offset;
				re_interpreter_copy_match(interp);
			}
			break;
		}
		--search_range;
		if (search_range == 0)
			break;
		--match_offset;
		re_interpreter_inptr_in_reverse1(&used_inptr);
		interp->ri_in = used_inptr;
	}

	/* Cleanup */
	re_interpreter_fini(interp);
	return result;
err:
	return -error;
}



#undef delta16_get
#undef DBG_memset
#undef ascii_islf
#undef HAVE_TRACE
#undef TRACE


/* Exports */
DEFINE_PUBLIC_ALIAS(re_exec_match, libre_exec_match);
DEFINE_PUBLIC_ALIAS(re_exec_search, libre_exec_search);
DEFINE_PUBLIC_ALIAS(re_exec_search_noepsilon, libre_exec_search_noepsilon);
DEFINE_PUBLIC_ALIAS(re_exec_rsearch, libre_exec_rsearch);

DECL_END

#endif /* !GUARD_LIBREGEX_REGEXEC_C */
