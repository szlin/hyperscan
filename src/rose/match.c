/*
 * Copyright (c) 2015-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "catchup.h"
#include "counting_miracle.h"
#include "infix.h"
#include "match.h"
#include "miracle.h"
#include "program_runtime.h"
#include "rose_program.h"
#include "rose.h"
#include "som/som_runtime.h"
#include "util/bitutils.h"
#include "util/fatbit.h"

#if defined(DEBUG) || defined(DUMP_SUPPORT)
#include "util/compare.h"
/** A debugging crutch: print a hex-escaped version of the match for our
 * perusal. The start and end offsets are stream offsets. */
static UNUSED
void printMatch(const struct core_info *ci, u64a start, u64a end) {
    assert(start <= end);
    assert(end <= ci->buf_offset + ci->len);

    printf("'");
    u64a i = start;
    for (; i <= MIN(ci->buf_offset, end); i++) {
        u64a h_idx = ci->buf_offset - i;
        u8 c = h_idx >= ci->hlen ? '?' : ci->hbuf[ci->hlen - h_idx - 1];
        if (ourisprint(c) && c != '\'') {
            printf("%c", c);
        } else {
            printf("\\x%02x", c);
        }
    }
    for (; i <= end; i++) {
        u64a b_idx = i - ci->buf_offset - 1;
        u8 c = b_idx >= ci->len ? '?' : ci->buf[b_idx];
        if (ourisprint(c) && c != '\'') {
            printf("%c", c);
        } else {
            printf("\\x%02x", c);
        }
    }
    printf("'");
}
#endif

hwlmcb_rv_t roseDelayRebuildCallback(size_t start, size_t end, u32 id,
                                     void *ctx) {
    struct hs_scratch *scratch = ctx;
    struct RoseContext *tctx = &scratch->tctxt;
    struct core_info *ci = &scratch->core_info;
    const struct RoseEngine *t = ci->rose;
    size_t rb_len = MIN(ci->hlen, t->delayRebuildLength);

    u64a real_end = ci->buf_offset - rb_len + end + 1; // index after last byte

#ifdef DEBUG
    DEBUG_PRINTF("REBUILD MATCH id=%u offsets=[%llu,%llu]: ", id,
                 start + ci->buf_offset - rb_len, real_end);
    printMatch(ci, start + ci->buf_offset - rb_len, real_end);
    printf("\n");
#endif

    DEBUG_PRINTF("STATE groups=0x%016llx\n", tctx->groups);

    const u32 *delayRebuildPrograms =
        getByOffset(t, t->litDelayRebuildProgramOffset);
    assert(id < t->literalCount);
    const u32 program = delayRebuildPrograms[id];

    if (program) {
        const u64a som = 0;
        const size_t match_len = end - start + 1;
        const char in_anchored = 0;
        const char in_catchup = 0;
        const char from_mpv = 0;
        const char skip_mpv_catchup = 0;
        UNUSED hwlmcb_rv_t rv =
            roseRunProgram(t, scratch, program, som, real_end, match_len,
                           in_anchored, in_catchup, from_mpv, skip_mpv_catchup);
        assert(rv != HWLM_TERMINATE_MATCHING);
    }

    /* we are just repopulating the delay queue, groups should be
     * already set from the original scan. */

    return tctx->groups;
}

static really_inline
hwlmcb_rv_t ensureMpvQueueFlushed(const struct RoseEngine *t,
                                  struct hs_scratch *scratch, u32 qi, s64a loc,
                                  char in_chained) {
    return ensureQueueFlushed_i(t, scratch, qi, loc, 1, in_chained);
}

static rose_inline
void recordAnchoredLiteralMatch(const struct RoseEngine *t,
                                struct hs_scratch *scratch, u32 literal_id,
                                u64a end) {
    assert(end);
    struct fatbit **anchoredLiteralRows = getAnchoredLiteralLog(scratch);

    DEBUG_PRINTF("record %u @ %llu\n", literal_id, end);

    if (!bf64_set(&scratch->al_log_sum, end - 1)) {
        // first time, clear row
        DEBUG_PRINTF("clearing %llu/%u\n", end - 1, t->anchored_count);
        fatbit_clear(anchoredLiteralRows[end - 1]);
    }

    u32 rel_idx = literal_id - t->anchored_base_id;
    DEBUG_PRINTF("record %u @ %llu index %u/%u\n", literal_id, end, rel_idx,
                 t->anchored_count);
    assert(rel_idx < t->anchored_count);
    fatbit_set(anchoredLiteralRows[end - 1], t->anchored_count, rel_idx);
}

hwlmcb_rv_t roseHandleChainMatch(const struct RoseEngine *t,
                                 struct hs_scratch *scratch, u32 event,
                                 u64a top_squash_distance, u64a end,
                                 char in_catchup) {
    assert(event == MQE_TOP || event >= MQE_TOP_FIRST);
    struct core_info *ci = &scratch->core_info;

    u8 *aa = getActiveLeafArray(t, scratch->core_info.state);
    u32 aaCount = t->activeArrayCount;
    struct fatbit *activeQueues = scratch->aqa;
    u32 qCount = t->queueCount;

    const u32 qi = 0; /* MPV is always queue 0 if it exists */
    struct mq *q = &scratch->queues[qi];
    const struct NfaInfo *info = getNfaInfoByQueue(t, qi);

    s64a loc = (s64a)end - ci->buf_offset;
    assert(loc <= (s64a)ci->len && loc >= -(s64a)ci->hlen);

    if (!mmbit_set(aa, aaCount, qi)) {
        initQueue(q, qi, t, scratch);
        nfaQueueInitState(q->nfa, q);
        pushQueueAt(q, 0, MQE_START, loc);
        fatbit_set(activeQueues, qCount, qi);
    } else if (info->no_retrigger) {
        DEBUG_PRINTF("yawn\n");
        /* nfa only needs one top; we can go home now */
        return HWLM_CONTINUE_MATCHING;
    } else if (!fatbit_set(activeQueues, qCount, qi)) {
        initQueue(q, qi, t, scratch);
        loadStreamState(q->nfa, q, 0);
        pushQueueAt(q, 0, MQE_START, 0);
    } else if (isQueueFull(q)) {
        DEBUG_PRINTF("queue %u full -> catching up nfas\n", qi);
        /* we know it is a chained nfa and the suffixes/outfixes must already
         * be known to be consistent */
        if (ensureMpvQueueFlushed(t, scratch, qi, loc, in_catchup)
            == HWLM_TERMINATE_MATCHING) {
            DEBUG_PRINTF("terminating...\n");
            return HWLM_TERMINATE_MATCHING;
        }
    }

    if (top_squash_distance) {
        assert(q->cur != q->end);
        struct mq_item *last = &q->items[q->end - 1];
        if (last->type == event
            && last->location >= loc - (s64a)top_squash_distance) {
            last->location = loc;
            goto event_enqueued;
        }
    }

    pushQueue(q, event, loc);

event_enqueued:
    if (q_cur_loc(q) == (s64a)ci->len) {
        /* we may not run the nfa; need to ensure state is fine  */
        DEBUG_PRINTF("empty run\n");
        pushQueueNoMerge(q, MQE_END, loc);
        char alive = nfaQueueExec(q->nfa, q, loc);
        if (alive) {
            scratch->tctxt.mpv_inactive = 0;
            q->cur = q->end = 0;
            pushQueueAt(q, 0, MQE_START, loc);
        } else {
            mmbit_unset(aa, aaCount, qi);
            fatbit_unset(scratch->aqa, qCount, qi);
        }
    }

    DEBUG_PRINTF("added mpv event at %lld\n", loc);
    scratch->tctxt.next_mpv_offset = 0; /* the top event may result in matches
                                         * earlier than expected */
    return HWLM_CONTINUE_MATCHING;
}

int roseAnchoredCallback(u64a end, u32 id, void *ctx) {
    struct hs_scratch *scratch = ctx;
    struct RoseContext *tctxt = &scratch->tctxt;
    struct core_info *ci = &scratch->core_info;
    const struct RoseEngine *t = ci->rose;

    u64a real_end = ci->buf_offset + end; // index after last byte

    DEBUG_PRINTF("MATCH id=%u offsets=[???,%llu]\n", id, real_end);
    DEBUG_PRINTF("STATE groups=0x%016llx\n", tctxt->groups);

    if (can_stop_matching(scratch)) {
        DEBUG_PRINTF("received a match when we're already dead!\n");
        return MO_HALT_MATCHING;
    }

    const size_t match_len = 0;

    /* delayed literals need to be delivered before real literals; however
     * delayed literals only come from the floating table so if we are going
     * to deliver a literal here it must be too early for a delayed literal */

    /* no history checks from anchored region and we are before the flush
     * boundary */

    if (real_end <= t->floatingMinLiteralMatchOffset) {
        roseFlushLastByteHistory(t, scratch, real_end);
        tctxt->lastEndOffset = real_end;
    }

    const u32 *programs = getByOffset(t, t->litProgramOffset);
    assert(id < t->literalCount);
    const u64a som = 0;
    const char in_anchored = 1;
    const char in_catchup = 0;
    const char from_mpv = 0;
    const char skip_mpv_catchup = 0;
    if (roseRunProgram(t, scratch, programs[id], som, real_end, match_len,
                       in_anchored, in_catchup, from_mpv,
                       skip_mpv_catchup) == HWLM_TERMINATE_MATCHING) {
        assert(can_stop_matching(scratch));
        DEBUG_PRINTF("caller requested termination\n");
        return MO_HALT_MATCHING;
    }

    DEBUG_PRINTF("DONE groups=0x%016llx\n", tctxt->groups);

    if (real_end > t->floatingMinLiteralMatchOffset) {
        recordAnchoredLiteralMatch(t, scratch, id, real_end);
    }

    return MO_CONTINUE_MATCHING;
}

// Rose match-processing workhorse
/* assumes not in_anchored */
static really_inline
hwlmcb_rv_t roseProcessMatch(const struct RoseEngine *t,
                             struct hs_scratch *scratch, u64a end,
                             size_t match_len, u32 id) {
    DEBUG_PRINTF("id=%u\n", id);
    const u32 *programs = getByOffset(t, t->litProgramOffset);
    assert(id < t->literalCount);
    const u64a som = 0;
    const char in_anchored = 0;
    const char in_catchup = 0;
    const char from_mpv = 0;
    const char skip_mpv_catchup = 0;
    return roseRunProgram(t, scratch, programs[id], som, end, match_len,
                          in_anchored, in_catchup, from_mpv, skip_mpv_catchup);
}

static rose_inline
hwlmcb_rv_t playDelaySlot(const struct RoseEngine *t,
                          struct hs_scratch *scratch,
                          struct fatbit **delaySlots, u32 vicIndex,
                          u64a offset) {
    /* assert(!tctxt->in_anchored); */
    assert(vicIndex < DELAY_SLOT_COUNT);
    const struct fatbit *vicSlot = delaySlots[vicIndex];
    u32 delay_count = t->delay_count;

    if (offset < t->floatingMinLiteralMatchOffset) {
        DEBUG_PRINTF("too soon\n");
        return HWLM_CONTINUE_MATCHING;
    }

    struct RoseContext *tctxt = &scratch->tctxt;
    roseFlushLastByteHistory(t, scratch, offset);
    tctxt->lastEndOffset = offset;

    for (u32 it = fatbit_iterate(vicSlot, delay_count, MMB_INVALID);
         it != MMB_INVALID; it = fatbit_iterate(vicSlot, delay_count, it)) {
        u32 literal_id = t->delay_base_id + it;

        UNUSED rose_group old_groups = tctxt->groups;

        DEBUG_PRINTF("DELAYED MATCH id=%u offset=%llu\n", literal_id, offset);
        hwlmcb_rv_t rv = roseProcessMatch(t, scratch, offset, 0, literal_id);
        DEBUG_PRINTF("DONE groups=0x%016llx\n", tctxt->groups);

        /* delayed literals can't safely set groups.
         * However we may be setting groups that successors already have
         * worked out that we don't need to match the group */
        DEBUG_PRINTF("groups in %016llx out %016llx\n", old_groups,
                     tctxt->groups);

        if (rv == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }
    }

    return HWLM_CONTINUE_MATCHING;
}

static really_inline
hwlmcb_rv_t flushAnchoredLiteralAtLoc(const struct RoseEngine *t,
                                      struct hs_scratch *scratch,
                                      u32 curr_loc) {
    struct RoseContext *tctxt = &scratch->tctxt;
    struct fatbit *curr_row = getAnchoredLiteralLog(scratch)[curr_loc - 1];
    u32 region_width = t->anchored_count;

    DEBUG_PRINTF("report matches at curr loc\n");
    for (u32 it = fatbit_iterate(curr_row, region_width, MMB_INVALID);
         it != MMB_INVALID; it = fatbit_iterate(curr_row, region_width, it)) {
        DEBUG_PRINTF("it = %u/%u\n", it, region_width);
        u32 literal_id = t->anchored_base_id + it;

        rose_group old_groups = tctxt->groups;
        DEBUG_PRINTF("ANCH REPLAY MATCH id=%u offset=%u\n", literal_id,
                     curr_loc);
        hwlmcb_rv_t rv = roseProcessMatch(t, scratch, curr_loc, 0, literal_id);
        DEBUG_PRINTF("DONE groups=0x%016llx\n", tctxt->groups);

        /* anchored literals can't safely set groups.
         * However we may be setting groups that successors already
         * have worked out that we don't need to match the group */
        DEBUG_PRINTF("groups in %016llx out %016llx\n", old_groups,
                     tctxt->groups);
        tctxt->groups &= old_groups;

        if (rv == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }
    }

    /* clear row; does not invalidate iteration */
    bf64_unset(&scratch->al_log_sum, curr_loc - 1);

    return HWLM_CONTINUE_MATCHING;
}

static really_inline
u32 anchored_it_begin(struct hs_scratch *scratch) {
    struct RoseContext *tctxt = &scratch->tctxt;
    if (tctxt->lastEndOffset >= scratch->anchored_literal_region_len) {
        return MMB_INVALID;
    }
    u32 begin = tctxt->lastEndOffset;
    begin--;

    return bf64_iterate(scratch->al_log_sum, begin);
}

static really_inline
hwlmcb_rv_t flushAnchoredLiterals(const struct RoseEngine *t,
                                  struct hs_scratch *scratch,
                                  u32 *anchored_it_param, u64a to_off) {
    struct RoseContext *tctxt = &scratch->tctxt;
    u32 anchored_it = *anchored_it_param;
    /* catch up any remaining anchored matches */
    for (; anchored_it != MMB_INVALID && anchored_it < to_off;
         anchored_it = bf64_iterate(scratch->al_log_sum, anchored_it)) {
        assert(anchored_it < scratch->anchored_literal_region_len);
        DEBUG_PRINTF("loc_it = %u\n", anchored_it);
        u32 curr_off = anchored_it + 1;
        roseFlushLastByteHistory(t, scratch, curr_off);
        tctxt->lastEndOffset = curr_off;

        if (flushAnchoredLiteralAtLoc(t, scratch, curr_off)
            == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }
    }

    *anchored_it_param = anchored_it;
    return HWLM_CONTINUE_MATCHING;
}

static really_inline
hwlmcb_rv_t playVictims(const struct RoseEngine *t, struct hs_scratch *scratch,
                        u32 *anchored_it, u64a lastEnd, u64a victimDelaySlots,
                        struct fatbit **delaySlots) {
    while (victimDelaySlots) {
        u32 vic = findAndClearLSB_64(&victimDelaySlots);
        DEBUG_PRINTF("vic = %u\n", vic);
        u64a vicOffset = vic + (lastEnd & ~(u64a)DELAY_MASK);

        if (flushAnchoredLiterals(t, scratch, anchored_it, vicOffset)
            == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }

        if (playDelaySlot(t, scratch, delaySlots, vic % DELAY_SLOT_COUNT,
                          vicOffset) == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }
    }

    return HWLM_CONTINUE_MATCHING;
}

/* call flushQueuedLiterals instead */
hwlmcb_rv_t flushQueuedLiterals_i(const struct RoseEngine *t,
                                  struct hs_scratch *scratch, u64a currEnd) {
    struct RoseContext *tctxt = &scratch->tctxt;
    u64a lastEnd = tctxt->delayLastEndOffset;
    DEBUG_PRINTF("flushing backed up matches @%llu up from %llu\n", currEnd,
                 lastEnd);

    assert(currEnd != lastEnd); /* checked in main entry point */

    u32 anchored_it = anchored_it_begin(scratch);

    if (!tctxt->filledDelayedSlots) {
        DEBUG_PRINTF("no delayed, no flush\n");
        goto anchored_leftovers;
    }

    {
        struct fatbit **delaySlots = getDelaySlots(scratch);

        u32 lastIndex = lastEnd & DELAY_MASK;
        u32 currIndex = currEnd & DELAY_MASK;

        int wrapped = (lastEnd | DELAY_MASK) < currEnd;

        u64a victimDelaySlots; /* needs to be twice as wide as the number of
                                * slots. */

        DEBUG_PRINTF("hello %08x\n", tctxt->filledDelayedSlots);
        if (!wrapped) {
            victimDelaySlots = tctxt->filledDelayedSlots;

            DEBUG_PRINTF("unwrapped %016llx %08x\n", victimDelaySlots,
                         tctxt->filledDelayedSlots);
            /* index vars < 32 so 64bit shifts are safe */

            /* clear all slots at last index and below, */
            victimDelaySlots &= ~((1LLU << (lastIndex + 1)) - 1);

            /* clear all slots above curr index */
            victimDelaySlots &= (1LLU << (currIndex + 1)) - 1;

            tctxt->filledDelayedSlots &= ~victimDelaySlots;

            DEBUG_PRINTF("unwrapped %016llx %08x\n", victimDelaySlots,
                         tctxt->filledDelayedSlots);
        } else {
            DEBUG_PRINTF("wrapped %08x\n", tctxt->filledDelayedSlots);

            /* 1st half: clear all slots at last index and below, */
            u64a first_half = tctxt->filledDelayedSlots;
            first_half &= ~((1ULL << (lastIndex + 1)) - 1);
            tctxt->filledDelayedSlots &= (1ULL << (lastIndex + 1)) - 1;

            u64a second_half = tctxt->filledDelayedSlots;

            if (currEnd > lastEnd + DELAY_SLOT_COUNT) {
                /* 2nd half: clear all slots above last index */
                second_half &= (1ULL << (lastIndex + 1)) - 1;
            } else {
                /* 2nd half: clear all slots above curr index */
                second_half &= (1ULL << (currIndex + 1)) - 1;
            }
            tctxt->filledDelayedSlots &= ~second_half;

            victimDelaySlots = first_half | (second_half << DELAY_SLOT_COUNT);

            DEBUG_PRINTF("-- %016llx %016llx = %016llx (li %u)\n", first_half,
                         second_half, victimDelaySlots, lastIndex);
        }

        if (playVictims(t, scratch, &anchored_it, lastEnd, victimDelaySlots,
                        delaySlots) == HWLM_TERMINATE_MATCHING) {
            return HWLM_TERMINATE_MATCHING;
        }
    }

anchored_leftovers:;
    hwlmcb_rv_t rv = flushAnchoredLiterals(t, scratch, &anchored_it, currEnd);
    tctxt->delayLastEndOffset = currEnd;
    return rv;
}

hwlmcb_rv_t roseCallback(size_t start, size_t end, u32 id, void *ctxt) {
    struct hs_scratch *scratch = ctxt;
    struct RoseContext *tctx = &scratch->tctxt;
    const struct RoseEngine *t = scratch->core_info.rose;

    u64a real_end = end + tctx->lit_offset_adjust;

#if defined(DEBUG)
    DEBUG_PRINTF("MATCH id=%u offsets=[%llu,%llu]: ", id,
                 start + tctx->lit_offset_adjust, real_end);
    printMatch(&scratch->core_info, start + tctx->lit_offset_adjust, real_end);
    printf("\n");
#endif
    DEBUG_PRINTF("last end %llu\n", tctx->lastEndOffset);

    DEBUG_PRINTF("STATE groups=0x%016llx\n", tctx->groups);

    if (can_stop_matching(scratch)) {
        DEBUG_PRINTF("received a match when we're already dead!\n");
        return HWLM_TERMINATE_MATCHING;
    }

    hwlmcb_rv_t rv = flushQueuedLiterals(t, scratch, real_end);
    /* flushDelayed may have advanced tctx->lastEndOffset */

    if (real_end >= t->floatingMinLiteralMatchOffset) {
        roseFlushLastByteHistory(t, scratch, real_end);
        tctx->lastEndOffset = real_end;
    }

    if (rv == HWLM_TERMINATE_MATCHING) {
        return HWLM_TERMINATE_MATCHING;
    }

    size_t match_len = end - start + 1;
    rv = roseProcessMatch(t, scratch, real_end, match_len, id);

    DEBUG_PRINTF("DONE groups=0x%016llx\n", tctx->groups);

    if (rv != HWLM_TERMINATE_MATCHING) {
        return tctx->groups;
    }

    assert(can_stop_matching(scratch));
    DEBUG_PRINTF("user requested halt\n");
    return HWLM_TERMINATE_MATCHING;
}

/**
 * \brief Match callback adaptor used for matches from pure-literal cases.
 *
 * Literal match IDs in this path run limited Rose programs that do not use
 * Rose state (which is not initialised in the pure-literal path). They can
 * still, for example, check lookarounds or literal masks.
 */
hwlmcb_rv_t rosePureLiteralCallback(size_t start, size_t end, u32 id,
                                    void *context) {
    DEBUG_PRINTF("start=%zu, end=%zu, id=%u\n", start, end, id);
    struct hs_scratch *scratch = context;
    struct core_info *ci = &scratch->core_info;
    const u64a real_end = (u64a)end + ci->buf_offset + 1;
    const u64a som = 0;
    const size_t match_len = end - start + 1;
    const struct RoseEngine *rose = ci->rose;
    const u32 *programs = getByOffset(rose, rose->litProgramOffset);
    assert(id < rose->literalCount);
    const char in_anchored = 0;
    const char in_catchup = 0;
    const char from_mpv = 0;
    const char skip_mpv_catchup = 0;
    return roseRunProgram(rose, scratch, programs[id], som, real_end, match_len,
                          in_anchored, in_catchup, from_mpv, skip_mpv_catchup);
}

/**
 * \brief Execute a boundary report program.
 *
 * Returns MO_HALT_MATCHING if the stream is exhausted or the user has
 * instructed us to halt, or MO_CONTINUE_MATCHING otherwise.
 */
int roseRunBoundaryProgram(const struct RoseEngine *rose, u32 program,
                           u64a stream_offset, struct hs_scratch *scratch) {
    DEBUG_PRINTF("running boundary program at offset %u\n", program);

    if (can_stop_matching(scratch)) {
        DEBUG_PRINTF("can stop matching\n");
        return MO_HALT_MATCHING;
    }

    if (rose->hasSom && scratch->deduper.current_report_offset == ~0ULL) {
        /* we cannot delay the initialization of the som deduper logs any longer
         * as we are reporting matches. This is done explicitly as we are
         * shortcutting the som handling in the vacuous repeats as we know they
         * all come from non-som patterns. */
        fatbit_clear(scratch->deduper.som_log[0]);
        fatbit_clear(scratch->deduper.som_log[1]);
        scratch->deduper.som_log_dirty = 0;
    }

    // Keep assertions in program report path happy. At offset zero, there can
    // have been no earlier reports. At EOD, all earlier reports should have
    // been handled and we will have been caught up to the stream offset by the
    // time we are running boundary report programs.
    scratch->tctxt.minMatchOffset = stream_offset;

    const u64a som = 0;
    const size_t match_len = 0;
    const char in_anchored = 0;
    const char in_catchup = 0;
    const char from_mpv = 0;
    const char skip_mpv_catchup = 0;
    hwlmcb_rv_t rv =
        roseRunProgram(rose, scratch, program, som, stream_offset, match_len,
                       in_anchored, in_catchup, from_mpv, skip_mpv_catchup);
    if (rv == HWLM_TERMINATE_MATCHING) {
        return MO_HALT_MATCHING;
    }

    return MO_CONTINUE_MATCHING;
}

static really_inline
int roseReportAdaptor_i(u64a som, u64a offset, ReportID id, void *context) {
    struct hs_scratch *scratch = context;
    assert(scratch && scratch->magic == SCRATCH_MAGIC);

    const struct RoseEngine *rose = scratch->core_info.rose;

    // Our match ID is the program offset.
    const u32 program = id;
    const size_t match_len = 0; // Unused in this path.
    const char in_anchored = 0;
    const char in_catchup = 0;
    const char from_mpv = 0;
    const char skip_mpv_catchup = 1;
    hwlmcb_rv_t rv =
        roseRunProgram(rose, scratch, program, som, offset, match_len,
                       in_anchored, in_catchup, from_mpv, skip_mpv_catchup);
    if (rv == HWLM_TERMINATE_MATCHING) {
        return MO_HALT_MATCHING;
    }

    return can_stop_matching(scratch) ? MO_HALT_MATCHING : MO_CONTINUE_MATCHING;
}

int roseReportAdaptor(u64a offset, ReportID id, void *context) {
    DEBUG_PRINTF("offset=%llu, id=%u\n", offset, id);
    return roseReportAdaptor_i(0, offset, id, context);
}

int roseReportSomAdaptor(u64a som, u64a offset, ReportID id, void *context) {
    DEBUG_PRINTF("som=%llu, offset=%llu, id=%u\n", som, offset, id);
    return roseReportAdaptor_i(som, offset, id, context);
}
