/*
 * Copyright (c) 2015, Intel Corporation
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

/** \file
 * \brief Large Bounded Repeat (LBR): dump code.
 */

#include "config.h"

#include "lbr_dump.h"

#include "lbr_internal.h"
#include "nfa_dump_internal.h"
#include "nfa_internal.h"
#include "repeat_internal.h"
#include "shufticompile.h"
#include "trufflecompile.h"
#include "util/charreach.h"
#include "util/dump_charclass.h"

#ifndef DUMP_SUPPORT
#error No dump support!
#endif

namespace ue2 {

void nfaExecLbrDot_dumpDot(UNUSED const NFA *nfa, UNUSED FILE *f) {
    // No impl
}

void nfaExecLbrVerm_dumpDot(UNUSED const NFA *nfa, UNUSED FILE *f) {
    // No impl
}

void nfaExecLbrNVerm_dumpDot(UNUSED const NFA *nfa, UNUSED FILE *f) {
    // No impl
}

void nfaExecLbrShuf_dumpDot(UNUSED const NFA *nfa, UNUSED FILE *f) {
    // No impl
}

void nfaExecLbrTruf_dumpDot(UNUSED const NFA *nfa, UNUSED FILE *f) {
    // No impl
}

static
void lbrDumpCommon(const lbr_common *lc, FILE *f) {
    const RepeatInfo *info
        = (const RepeatInfo *)((const char *)lc + lc->repeatInfoOffset);
    fprintf(f, "Limited Bounded Repeat\n");
    fprintf(f, "\n");
    fprintf(f, "repeat model:  %s\n", repeatTypeName(info->type));
    fprintf(f, "repeat bounds: {%u, %u}\n", info->repeatMin,
            info->repeatMax);
    fprintf(f, "report id:     %u\n", lc->report);
    fprintf(f, "\n");
    fprintf(f, "min period: %u\n", info->minPeriod);
}

void nfaExecLbrDot_dumpText(const NFA *nfa, FILE *f) {
    assert(nfa);
    assert(nfa->type == LBR_NFA_Dot);
    const lbr_dot *ld = (const lbr_dot *)getImplNfa(nfa);
    lbrDumpCommon(&ld->common, f);
    fprintf(f, "DOT model\n");
    fprintf(f, "\n");
    dumpTextReverse(nfa, f);
}

void nfaExecLbrVerm_dumpText(const NFA *nfa, FILE *f) {
    assert(nfa);
    assert(nfa->type == LBR_NFA_Verm);
    const lbr_verm *lv = (const lbr_verm *)getImplNfa(nfa);
    lbrDumpCommon(&lv->common, f);
    fprintf(f, "VERM model, scanning for 0x%02x\n", lv->c);
    fprintf(f, "\n");
    dumpTextReverse(nfa, f);
}

void nfaExecLbrNVerm_dumpText(const NFA *nfa, FILE *f) {
    assert(nfa);
    assert(nfa->type == LBR_NFA_NVerm);
    const lbr_verm *lv = (const lbr_verm *)getImplNfa(nfa);
    lbrDumpCommon(&lv->common, f);
    fprintf(f, "NEGATED VERM model, scanning for 0x%02x\n", lv->c);
    fprintf(f, "\n");
    dumpTextReverse(nfa, f);
}

void nfaExecLbrShuf_dumpText(const NFA *nfa, FILE *f) {
    assert(nfa);
    assert(nfa->type == LBR_NFA_Shuf);
    const lbr_shuf *ls = (const lbr_shuf *)getImplNfa(nfa);
    lbrDumpCommon(&ls->common, f);

    CharReach cr = shufti2cr(ls->mask_lo, ls->mask_hi);
    fprintf(f, "SHUF model, scanning for: %s (%zu chars)\n",
            describeClass(cr, 20, CC_OUT_TEXT).c_str(), cr.count());
    fprintf(f, "\n");
    dumpTextReverse(nfa, f);
}

void nfaExecLbrTruf_dumpText(const NFA *nfa, FILE *f) {
    assert(nfa);
    assert(nfa->type == LBR_NFA_Truf);
    const lbr_truf *lt = (const lbr_truf *)getImplNfa(nfa);
    lbrDumpCommon(&lt->common, f);

    CharReach cr = truffle2cr(lt->mask1, lt->mask2);
    fprintf(f, "TRUFFLE model, scanning for: %s (%zu chars)\n",
            describeClass(cr, 20, CC_OUT_TEXT).c_str(), cr.count());
    fprintf(f, "\n");
    dumpTextReverse(nfa, f);
}

} // namespace ue2
