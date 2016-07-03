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

#include "config.h"

#include "ue2common.h"
#include "grey.h"
#include "fdr/fdr.h"
#include "fdr/fdr_compile.h"
#include "fdr/fdr_compile_internal.h"
#include "fdr/fdr_engine_description.h"
#include "fdr/teddy_compile.h"
#include "fdr/teddy_engine_description.h"
#include "util/alloc.h"

#include "database.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <boost/random.hpp>

using namespace std;
using namespace testing;
using namespace ue2;

#define NO_TEDDY_FAIL_ALLOWED 0

#if(NO_TEDDY_FAIL_ALLOWED)
#define CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint) ASSERT_TRUE(fdr != nullptr)
#else
#define CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint)                                 \
    {                                                                          \
        auto descr = getTeddyDescription(hint);                                \
        if (descr && fdr == nullptr) {                                         \
            return; /* cannot build Teddy for this set of literals */          \
        } else {                                                               \
            ASSERT_TRUE(fdr != nullptr);                                       \
        }                                                                      \
    }
#endif

namespace {

struct match {
    size_t start;
    size_t end;
    u32 id;
    match(size_t start_in, size_t end_in, u32 id_in)
        : start(start_in), end(end_in), id(id_in) {}
    bool operator==(const match &b) const {
        return start == b.start && end == b.end && id == b.id;
    }
    bool operator<(const match &b) const {
        if (id < b.id) {
            return true;
        } else if (id == b.id) {
            if (start < b.start) {
                return true;
            } else if (start == b.start) {
                return end < b.end;
            }
        }
        return false;
    }
    match operator+(size_t adj) {
        return match(start + adj, end + adj, id);
    }
};

extern "C" {
static
hwlmcb_rv_t countCallback(UNUSED size_t start, UNUSED size_t end, u32,
                          void *ctxt) {
    if (ctxt) {
        ++*(u32 *)ctxt;
    }

    return HWLM_CONTINUE_MATCHING;
}

static
hwlmcb_rv_t decentCallback(size_t start, size_t end, u32 id, void *ctxt) {
    DEBUG_PRINTF("match %zu-%zu : %u\n", start, end, id);
    if (!ctxt) {
        return HWLM_CONTINUE_MATCHING;
    }

    vector<match> *out = (vector<match> *)ctxt;
    out->push_back(match(start, end, id));
    return HWLM_CONTINUE_MATCHING;
}

static
hwlmcb_rv_t decentCallbackT(size_t start, size_t end, u32 id, void *ctxt) {
    if (!ctxt) {
        return HWLM_TERMINATE_MATCHING;
    }

    vector<match> *out = (vector<match> *)ctxt;
    out->push_back(match(start, end, id));
    return HWLM_TERMINATE_MATCHING;
}

} // extern "C"

} // namespace

static
vector<u32> getValidFdrEngines() {
    const auto target = get_current_target();

    vector<u32> ret;

    vector<FDREngineDescription> fdr_descriptions;
    getFdrDescriptions(&fdr_descriptions);
    for (const FDREngineDescription &d : fdr_descriptions) {
        if (d.isValidOnTarget(target)) {
            ret.push_back(d.getID());
        }
    }

    vector<TeddyEngineDescription> teddy_descriptions;
    getTeddyDescriptions(&teddy_descriptions);
    for (const TeddyEngineDescription &d : teddy_descriptions) {
        if (d.isValidOnTarget(target)) {
            ret.push_back(d.getID());
        }
    }

    return ret;
}

class FDRp : public TestWithParam<u32> {
};

TEST_P(FDRp, Simple) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    const char data[] = "mnopqrabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890mnopqr";

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("mnopqr", 0, 0));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    fdrExec(fdr.get(), (const u8 *)data, sizeof(data), 0, decentCallback,
            &matches, HWLM_ALL_GROUPS);

    ASSERT_EQ(3U, matches.size());
    EXPECT_EQ(match(0, 5, 0), matches[0]);
    EXPECT_EQ(match(18, 23, 0), matches[1]);
    EXPECT_EQ(match(78, 83, 0), matches[2]);
}

TEST_P(FDRp, SimpleSingle) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    const char data[] = "mnopqrabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890m0m";

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("m", 0, 0));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    fdrExec(fdr.get(), (const u8 *)data, sizeof(data) - 1 /* skip nul */, 0,
            decentCallback, &matches, HWLM_ALL_GROUPS);

    ASSERT_EQ(4U, matches.size());
    EXPECT_EQ(match(0, 0, 0), matches[0]);
    EXPECT_EQ(match(18, 18, 0), matches[1]);
    EXPECT_EQ(match(78, 78, 0), matches[2]);
    EXPECT_EQ(match(80, 80, 0), matches[3]);
}

TEST_P(FDRp, MultiLocation) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("abc", 0, 1));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    const u32 testSize = 128;

    vector<u8> data(testSize, 0);

    for (u32 i = 0; i < testSize - 3; i++) {
        memcpy(data.data() + i, "abc", 3);
        vector<match> matches;
        fdrExec(fdr.get(), data.data(), testSize, 0, decentCallback, &matches,
                HWLM_ALL_GROUPS);
        ASSERT_EQ(1U, matches.size());
        EXPECT_EQ(match(i, i+2, 1), matches[0]);
        memset(data.data() + i, 0, 3);
    }
}

TEST_P(FDRp, Flood) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("aaaa", 0, 1));
    lits.push_back(hwlmLiteral("aaaaaaaa", 0, 2));
    lits.push_back(hwlmLiteral("baaaaaaaa", 0, 3));
    lits.push_back(hwlmLiteral("aaaaaaaab", 0, 4));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    const u32 testSize = 1024;
    vector<u8> data(testSize, 'a');

    vector<match> matches;
    fdrExec(fdr.get(), data.data(), testSize, 0, decentCallback, &matches,
            HWLM_ALL_GROUPS);
    ASSERT_EQ(testSize - 3 + testSize - 7, matches.size());
    EXPECT_EQ(match(0, 3, 1), matches[0]);
    EXPECT_EQ(match(1, 4, 1), matches[1]);
    EXPECT_EQ(match(2, 5, 1), matches[2]);
    EXPECT_EQ(match(3, 6, 1), matches[3]);

    u32 currentMatch = 4;
    for (u32 i = 7; i < testSize; i++, currentMatch += 2) {
        EXPECT_TRUE(
          (match(i - 3, i, 1) == matches[currentMatch] &&
           match(i - 7, i, 2) == matches[currentMatch+1]) ||
          (match(i - 7, i, 2) == matches[currentMatch+1] &&
           match(i - 3, i, 1) == matches[currentMatch])
        );
    }
}

TEST_P(FDRp, NoRepeat1) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    const char data[] = "mnopqrabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890m0m";

    vector<hwlmLiteral> lits
        = { hwlmLiteral("m", 0, 1, 0, HWLM_ALL_GROUPS, {}, {}) };

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    fdrExec(fdr.get(), (const u8 *)data, sizeof(data) - 1 /* skip nul */, 0,
            decentCallback, &matches, HWLM_ALL_GROUPS);

    ASSERT_EQ(1U, matches.size());
    EXPECT_EQ(match(0, 0, 0), matches[0]);
}

TEST_P(FDRp, NoRepeat2) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    const char data[] = "mnopqrabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890m0m";

    vector<hwlmLiteral> lits
        = { hwlmLiteral("m", 0, 1, 0, HWLM_ALL_GROUPS, {}, {}),
            hwlmLiteral("A", 0, 42) };

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    fdrExec(fdr.get(), (const u8 *)data, sizeof(data) - 1 /* skip nul */, 0,
            decentCallback, &matches, HWLM_ALL_GROUPS);

    ASSERT_EQ(3U, matches.size());
    EXPECT_EQ(match(0, 0, 0), matches[0]);
    EXPECT_EQ(match(78, 78, 0), matches[2]);
}

TEST_P(FDRp, NoRepeat3) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    const char data[] = "mnopqrabcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890m0m";

    vector<hwlmLiteral> lits
        = { hwlmLiteral("90m", 0, 1, 0, HWLM_ALL_GROUPS, {}, {}),
            hwlmLiteral("zA", 0, 1, 0, HWLM_ALL_GROUPS, {}, {}) };

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    fdrExec(fdr.get(), (const u8 *)data, sizeof(data) - 1 /* skip nul */, 0,
            decentCallback, &matches, HWLM_ALL_GROUPS);

    ASSERT_EQ(1U, matches.size());
    EXPECT_EQ(match(31, 32, 0), matches[0]);
}

/**
 * \brief Helper function wrapping the FDR streaming call that ensures it is
 * always safe to read 16 bytes before the end of the history buffer.
 */
static
hwlm_error_t safeExecStreaming(const FDR *fdr, const u8 *hbuf, size_t hlen,
                               const u8 *buf, size_t len, size_t start,
                               HWLMCallback cb, void *ctxt, hwlm_group_t groups,
                               u8 *stream_state) {
    array<u8, 16> wrapped_history = {{'0', '1', '2', '3', '4', '5', '6', '7',
                                      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'}};
    if (hlen < 16) {
        u8 *new_hbuf = wrapped_history.data() + 16 - hlen;
        memcpy(new_hbuf, hbuf, hlen);
        hbuf = new_hbuf;
    }
    return fdrExecStreaming(fdr, hbuf, hlen, buf, len, start, cb, ctxt, groups,
                            stream_state);
}

TEST_P(FDRp, SmallStreaming) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    vector<hwlmLiteral> lits = {hwlmLiteral("a", 1, 1),
                                hwlmLiteral("aardvark", 0, 10)};

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> expected, matches;
    expected.push_back(match(0, 0, 1));
    expected.push_back(match(1, 1, 1));
    expected.push_back(match(2, 2, 1));

    safeExecStreaming(fdr.get(), (const u8 *)"", 0, (const u8 *)"aaar", 4, 0,
                      decentCallback, &matches, HWLM_ALL_GROUPS, nullptr);
    for (u32 i = 0; i < MIN(expected.size(), matches.size()); i++) {
        EXPECT_EQ(expected[i], matches[i]);
    }
    ASSERT_TRUE(expected.size() == matches.size());
    expected.clear();
    matches.clear();

    expected.push_back(match(6, 6, 1));
    expected.push_back(match(1, 8, 10));

    safeExecStreaming(fdr.get(), (const u8 *)"aaar", 4, (const u8 *)"dvark", 5,
                      0, decentCallback, &matches, HWLM_ALL_GROUPS, nullptr);

    for (u32 i = 0; i < MIN(expected.size(), matches.size()); i++) {
        EXPECT_EQ(expected[i], matches[i] + 4);
    }
    ASSERT_EQ(expected.size(), matches.size());
}

TEST_P(FDRp, SmallStreaming2) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);

    vector<hwlmLiteral> lits = {hwlmLiteral("a", 1, 1),
                                hwlmLiteral("kk", 1, 2),
                                hwlmLiteral("aardvark", 0, 10)};

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> expected, matches;
    expected.push_back(match(6,6,1));
    expected.push_back(match(7,7,1));
    expected.push_back(match(11,11,1));
    expected.push_back(match(6,13,10));
    expected.push_back(match(13,14,2));
    expected.push_back(match(14,15,2));

    safeExecStreaming(fdr.get(), (const u8 *)"foobar", 6,
                      (const u8 *)"aardvarkkk", 10, 0, decentCallback, &matches,
                      HWLM_ALL_GROUPS, nullptr);

    for (u32 i = 0; i < MIN(expected.size(), matches.size()); i++) {
        EXPECT_EQ(expected[i], matches[i] + 6);
    }
    ASSERT_EQ(expected.size(), matches.size());
}

TEST_P(FDRp, LongLiteral) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);
    size_t sz;
    const u8 *data;
    vector<hwlmLiteral> lits;

    string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    string alpha4 = alpha+alpha+alpha+alpha;
    lits.push_back(hwlmLiteral(alpha4.c_str(), 0,10));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    u32 count = 0;

    data = (const u8 *)alpha4.c_str();
    sz = alpha4.size();

    fdrExec(fdr.get(), data, sz, 0, countCallback, &count, HWLM_ALL_GROUPS);
    EXPECT_EQ(1U, count);
    count = 0;
    fdrExec(fdr.get(), data, sz - 1, 0, countCallback, &count, HWLM_ALL_GROUPS);
    EXPECT_EQ(0U, count);
    count = 0;
    fdrExec(fdr.get(), data + 1, sz - 1, 0, countCallback, &count,
            HWLM_ALL_GROUPS);
    EXPECT_EQ(0U, count);
}

TEST_P(FDRp, VeryLongLiteral) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);
    vector<hwlmLiteral> lits;

    string s1000;
    for(int i = 0; i < 1000; i++) {
        s1000 += char('A' + i % 10);
    }

    string s66k;
    for(int i = 0; i < 66; i++) {
        s66k += s1000;
    }

    string corpus = s66k + s66k;
    lits.push_back(hwlmLiteral(s66k.c_str(), 0, 10));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    vector<match> matches;
    u32 rv = fdrExec(fdr.get(), (const u8 *)s66k.c_str(), s66k.size(), 0,
                     decentCallback, &matches, HWLM_ALL_GROUPS);
    EXPECT_EQ(0U, rv);
    ASSERT_EQ(1U, matches.size());
    ASSERT_EQ(match(0, 65999, 10), matches[0]);

    matches.clear();
    rv = fdrExec(fdr.get(), (const u8 *)corpus.c_str(), corpus.size(), 0,
                 decentCallback, &matches, HWLM_ALL_GROUPS);
    EXPECT_EQ(0U, rv);
    for (u32 i = 0; i < matches.size(); i++) {
        ASSERT_EQ(match(10 * i, 65999 + 10 * i, 10), matches[i]);
    }
    EXPECT_EQ(6601U, matches.size());
}

TEST_P(FDRp, moveByteStream) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);
    const char data[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345678901234567890";
    size_t data_len = strlen(data);

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("mnopqr", 0, 0));

    auto fdrTable0 = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdrTable0, hint);

    size_t size = fdrSize(fdrTable0.get());

    auto fdrTable = aligned_zmalloc_unique<FDR>(size);
    EXPECT_NE(nullptr, fdrTable);

    memcpy(fdrTable.get(), fdrTable0.get(), size);

    //  bugger up original
    for (size_t i = 0 ; i < size; i++) {
        ((char *)fdrTable0.get())[i] = (i % 2) ? 0xCA : 0xFE;
    }

    // check matches
    vector<match> matches;

    hwlm_error_t fdrStatus = fdrExec(fdrTable.get(), (const u8 *)data,
                                     data_len, 0, decentCallback, &matches,
                                     HWLM_ALL_GROUPS);
    ASSERT_EQ(0, fdrStatus);

    ASSERT_EQ(1U, matches.size());
    EXPECT_EQ(match(12, 17, 0), matches[0]);
}

TEST_P(FDRp, Stream1) {
    const u32 hint = GetParam();
    SCOPED_TRACE(hint);
    const char data1[] = "fffffffffffffffff";
    const char data2[] = "ffffuuuuuuuuuuuuu";
    size_t data_len1 = strlen(data1);
    size_t data_len2 = strlen(data2);
    hwlm_error_t fdrStatus = 1;

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("f", 0, 0));
    lits.push_back(hwlmLiteral("longsigislong", 0, 1));

    auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(), Grey());
    CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

    // check matches
    vector<match> matches;

    fdrStatus = safeExecStreaming(
        fdr.get(), (const u8 *)data1, data_len1, (const u8 *)data2, data_len2,
        0, decentCallback, &matches, HWLM_ALL_GROUPS, nullptr);
    ASSERT_EQ(0, fdrStatus);

    ASSERT_EQ(4U, matches.size());
    for (size_t i = 0; i < matches.size(); i++) {
        EXPECT_EQ(match(i, i, 0), matches[i]);
    }
}

INSTANTIATE_TEST_CASE_P(FDR, FDRp, ValuesIn(getValidFdrEngines()));

typedef struct {
    string pattern;
    unsigned char alien;
} pattern_alien_t;

// gtest helper
void PrintTo(const pattern_alien_t &t, ::std::ostream *os) {
    *os << "(" << t.pattern << ", " << t.alien << ")";
}

class FDRpp : public TestWithParam<tuple<u32, pattern_alien_t>> {};

// This test will check if matcher detects properly literals at the beginning
// and at the end of unaligned buffer. It will check as well that match does
// not happen if literal is partially (from 1 character up to full literal
// length) is out of searched buffer - "too early" and "too late" conditions
TEST_P(FDRpp, AlignAndTooEarly) {

    const size_t buf_alignment = 32;
    // Buffer should be big enough to hold two instances of matching literals
    // (up to 64 bytes each) and room for offset (up to 32 bytes)
    const size_t data_len = 5 * buf_alignment;

    const u32 hint = get<0>(GetParam());
    SCOPED_TRACE(hint);

    // pattern which is used to generate literals of variable size - from 1 to 64
    const string &pattern = get<1>(GetParam()).pattern;
    const size_t patLen = pattern.size();
    const unsigned char alien = get<1>(GetParam()).alien;

    // allocate aligned buffer
    auto dataBufAligned = shared_ptr<char>(
        (char *)aligned_malloc_internal(data_len, buf_alignment),
        aligned_free_internal);

    vector<hwlmLiteral> lits;
    for (size_t litLen = 1; litLen <= patLen; litLen++) {

        // building literal from pattern substring of variable length 1-64
        lits.push_back(hwlmLiteral(string(pattern, 0, litLen), 0, 0));
        auto fdr = fdrBuildTableHinted(lits, false, hint, get_current_target(),
                                       Grey());
        CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

        // check with buffer offset from aligned start from 0 to 31
        for (size_t i = 0; i < buf_alignment; i++) {
            // fill the whole buffer with 'alien' character
            memset(dataBufAligned.get(), alien, data_len);
            // put the matching substring to the beginning of unaligned buffer
            memcpy(dataBufAligned.get() + i, pattern.data(), litLen);
            // put the matching substring to the end of unaligned buffer
            memcpy(dataBufAligned.get() + i + 4 * buf_alignment - litLen,
                        pattern.data(), litLen);

            for (size_t j = 0; j <= litLen; j++) {
                vector<match> matches;
                hwlm_error_t fdrStatus = fdrExec(fdr.get(),
                        (const u8 *)dataBufAligned.get() + i + j,
                        4 * buf_alignment - j * 2, 0, decentCallback,
                        &matches, HWLM_ALL_GROUPS);
                ASSERT_EQ(0, fdrStatus);
                // j == 0 means that start and end matches are entirely within
                // searched buffer. Otherwise they are out of buffer boundaries
                // by j number of bytes - "too early" or "too late" conditions
                // j == litLen means that matches are completely put of searched buffer
                if (j == 0) {
                    // we should get two and only two matches - at the beginning and
                    // at the end of unaligned buffer
                    ASSERT_EQ(2U, matches.size());
                    ASSERT_EQ(match(0, litLen - 1, 0), matches[0]);
                    ASSERT_EQ(match(4 * buf_alignment - litLen, 4 * buf_alignment - 1, 0), matches[1]);
                    matches.clear();
                } else {
                    // "Too early" / "too late" condition - should not match anything
                    ASSERT_EQ(0U, matches.size());
                }
            }
        }
        lits.clear();
    }
}

static const pattern_alien_t test_pattern[] = {
        {"abaabaaabaaabbaaaaabaaaaabbaaaaaaabaabbaaaabaaaaaaaabbbbaaaaaaab", 'x'},
        {"zzzyyzyzyyyyzyyyyyzzzzyyyyyyyyzyyyyyyyzzzzzyzzzzzzzzzyzzyzzzzzzz", (unsigned char)'\x99'},
        {"abcdef lafjk askldfjklf alfqwei9rui 'gldgkjnooiuswfs138746453583", '\0'}
};

INSTANTIATE_TEST_CASE_P(FDR, FDRpp, Combine(ValuesIn(getValidFdrEngines()),
                                            ValuesIn(test_pattern)));

// This test generates an exhaustive set of short input buffers of length from
// 1 to 6 (1092 buffers) and 2750 buffers of length from 7 to >64 constructed
// from arbitrary set of short buffers. All buffers contain 3 characters from
// the alphabet given as a parameter to the test.
// Then it generates an exhaustive set of literals of length from 1 to 8
// containing first two characters from the same alphabet (510 literals)
// Literals are grouped by 32 to run search on each and every buffer.
// All resulting matches are checked.

// Fibonacci sequence is used to generate arbitrary buffers
unsigned long long fib (int n) {
    unsigned long long fib0 = 1, fib1 = 1, fib2 = 1;
    for (int i = 0; i < n; i++) {
        fib2 = fib1 + fib0;
        fib0 = fib1;
        fib1 = fib2;
    }
    return fib2;
}

class FDRpa : public TestWithParam<tuple<u32, array<unsigned char, 3>>> {};

TEST_P(FDRpa, ShortWritings) {
    const u32 hint = get<0>(GetParam());
    SCOPED_TRACE(hint);
    vector<string> bufs;

    // create exhaustive buffer set for up to 6 literals:

    const array<unsigned char, 3> &alphabet = get<1>(GetParam());

    for (int len = 1; len <= 6; len++) {
        for (int j = 0; j < (int)pow((double)3, len); j++) {
            string s;
            for (int k = 0; k < len; k++) {
                s += alphabet[(j / (int)pow((double)3, k) % 3)];
            }
            bufs.push_back(s);
        }
    }
    size_t buflen = bufs.size();

    // create arbitrary buffers from exhaustive set of previously generated 'short'

    for (int len = 7; len < 64; len++) {
        for (int i = 0; i < 10; i++) {
            string s;
            for(int j = 0; (int)s.size() < len; j++) {
                s += bufs[fib(i * 5 + j + (len - 6) * 10) % buflen];
            }
            bufs.push_back(s);
        }
    }

    // generate exhaustive set of literals of length from 1 to 8
    vector<string> pats;
    for (int len = 1; len <= 8; len++) {
        for (int j = 0; j < (int)pow((double)2, len); j++) {
            string s;
            for (int k = 0; k < len; k++) {
                s += alphabet[(j >> k) & 1];
            }
            pats.push_back(s);
        }
    }

    // run the literal matching through all generated literals
    for (size_t patIdx = 0; patIdx < pats.size();) {
        // group them in the sets of 32
        vector<hwlmLiteral> testSigs;
        for(int i = 0; i < 32 && patIdx < pats.size(); i++, patIdx++) {
            testSigs.push_back(hwlmLiteral(pats[patIdx], false, patIdx));
        }

        auto fdr = fdrBuildTableHinted(testSigs, false, hint,
                                       get_current_target(), Grey());

        CHECK_WITH_TEDDY_OK_TO_FAIL(fdr, hint);

        // run the literal matching for the prepared set of 32 literals
        // on each generated buffer
        for (size_t bufIdx = 0; bufIdx < bufs.size(); bufIdx++) {
            const string &buf = bufs[bufIdx];
            size_t bufLen = buf.size();

            vector<match> matches;
            hwlm_error_t fdrStatus = fdrExec(fdr.get(), (const u8 *)buf.data(),
                        bufLen, 0, decentCallback, &matches, HWLM_ALL_GROUPS);
            ASSERT_EQ(0, fdrStatus);

            // build the set of expected matches using standard
            // stl::string::compare() function
            vector<match> expMatches;
            for (size_t pIdx = 0; pIdx < testSigs.size(); pIdx++) {

                const string &pat = testSigs[pIdx].s;
                size_t patLen = pat.size();

                for (int j = 0; j <= (int)bufLen - (int)patLen; j++) {
                    if (!buf.compare(j, patLen, pat)) {
                        expMatches.push_back(match(j, j + patLen - 1,
                                                testSigs[pIdx].id));
                    }
                }
            }
            // compare the set obtained matches against expected ones
            sort(expMatches.begin(), expMatches.end());
            sort(matches.begin(), matches.end());
            ASSERT_EQ(expMatches, matches);
        }
    }
}

static const array<unsigned char, 3> test_alphabet[] = {
    { { 'a', 'b', 'x' } },
    { { 'x', 'y', 'z' } },
    { { '\0', 'A', '\x20' } },
    { { 'a', '\x20', (unsigned char)'\x99' } }
};

INSTANTIATE_TEST_CASE_P(FDR, FDRpa, Combine(ValuesIn(getValidFdrEngines()),
                                            ValuesIn(test_alphabet)));

TEST(FDR, FDRTermS) {
    const char data1[] = "fffffffffffffffff";
    const char data2[] = "ffffuuuuuuuuuuuuu";
    size_t data_len1 = strlen(data1);
    size_t data_len2 = strlen(data2);
    hwlm_error_t fdrStatus = 0;

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("f", 0, 0));
    lits.push_back(hwlmLiteral("ff", 0, 1));

    auto fdr = fdrBuildTable(lits, false, get_current_target(), Grey());
    ASSERT_TRUE(fdr != nullptr);

    // check matches
    vector<match> matches;

    fdrStatus = safeExecStreaming(
        fdr.get(), (const u8 *)data1, data_len1, (const u8 *)data2, data_len2,
        0, decentCallbackT, &matches, HWLM_ALL_GROUPS, nullptr);
    ASSERT_EQ(HWLM_TERMINATED, fdrStatus);

    ASSERT_EQ(1U, matches.size());
}

TEST(FDR, FDRTermB) {
    const char data1[] = "fffffffffffffffff";
    size_t data_len1 = strlen(data1);
    hwlm_error_t fdrStatus = 0;

    vector<hwlmLiteral> lits;
    lits.push_back(hwlmLiteral("f", 0, 0));
    lits.push_back(hwlmLiteral("ff", 0, 1));

    auto fdr = fdrBuildTable(lits, false, get_current_target(), Grey());
    ASSERT_TRUE(fdr != nullptr);

    // check matches
    vector<match> matches;

    fdrStatus = fdrExec(fdr.get(), (const u8 *)data1, data_len1,
                        0, decentCallbackT, &matches, HWLM_ALL_GROUPS);
    ASSERT_EQ(HWLM_TERMINATED, fdrStatus);

    ASSERT_EQ(1U, matches.size());
}

TEST(FDR, ManyLengths) {
    // UE-2400: we had a crash due to div by zero in the compiler when given a
    // set of literals with precisely 512 different lengths.
    const u32 num = 512;
    vector<hwlmLiteral> lits;
    char c = 0;
    string s;
    for (u32 i = 0; i < num; i++) {
        s.push_back(c++);
        lits.push_back(hwlmLiteral(s, false, i + 1));
    }

    auto fdr = fdrBuildTable(lits, false, get_current_target(), Grey());
    ASSERT_TRUE(fdr != nullptr);

    // Confirm that we can scan against this FDR table as well.

    vector<match> matches;

    hwlm_error_t fdrStatus =
        fdrExec(fdr.get(), (const u8 *)s.c_str(), s.size(), 0, decentCallback,
                &matches, HWLM_ALL_GROUPS);
    ASSERT_EQ(HWLM_SUCCESS, fdrStatus);

    ASSERT_EQ(768U, matches.size());
}
