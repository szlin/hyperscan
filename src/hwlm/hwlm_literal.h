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

/** \file
 * \brief Hamster Wheel Literal Matcher: literal representation at build time.
 */

#ifndef HWLM_LITERAL_H
#define HWLM_LITERAL_H

#include "hwlm.h"
#include "ue2common.h"

#include <string>
#include <vector>

namespace ue2 {

/** \brief Max length of the hwlmLiteral::msk and hwlmLiteral::cmp vectors. */
#define HWLM_MASKLEN 8

/** \brief Class representing a literal, fed to \ref hwlmBuild. */
struct hwlmLiteral {
    std::string s; //!< \brief The literal itself.

    /** \brief The ID to pass to the callback if this literal matches.
     *
     * Note that the special value 0xFFFFFFFF is reserved for internal use and
     * should not be used. */
    u32 id;

    bool nocase; //!< \brief True if literal is case-insensitive.

    /** \brief Matches for runs of this literal can be quashed.
     *
     * Advisory flag meaning that there is no value in returning runs of
     * additional matches for a literal after the first one, so such matches
     * can be quashed by the literal matcher. */
    bool noruns;

    /** \brief Set of groups that literal belongs to.
     *
     * Use \ref HWLM_ALL_GROUPS for a literal that could match regardless of
     * the groups that are switched on. */
    hwlm_group_t groups;

    /** \brief Supplementary comparison mask.
     *
     * These two values add a supplementary comparison that is done over the
     * final 8 bytes of the string -- if v is those bytes, then the string must
     * match as well as (v & msk) == cmp.
     *
     * An empty msk is the safe way of not adding any comparison to the string
     * unnecessarily filling in msk may turn off optimizations.
     *
     * The msk/cmp mechanism must NOT place a value into the literal that
     * conflicts with the contents of the string, but can be allowed to add
     * additional power within the string -- for example, to allow some case
     * sensitivity within a case-insensitive string.

     * Values are stored in memory order -- i.e. the last byte of the mask
     * corresponds to the last byte of the string. Both vectors must be the
     * same size, and must not exceed \ref HWLM_MASKLEN in length.
     */
    std::vector<u8> msk;

    /** \brief Supplementary comparison value.
     *
     * See documentation for \ref msk.
     */
    std::vector<u8> cmp;

    /** \brief Complete constructor, takes group information and msk/cmp.
     *
     * This constructor takes a msk/cmp pair. Both must be vectors of length <=
     * \ref HWLM_MASKLEN. */
    hwlmLiteral(const std::string &s_in, bool nocase_in, bool noruns_in,
                u32 id_in, hwlm_group_t groups_in,
                const std::vector<u8> &msk_in, const std::vector<u8> &cmp_in);

    /** \brief Simple constructor: no group information, no msk/cmp. */
    hwlmLiteral(const std::string &s_in, bool nocase_in, u32 id_in)
        : hwlmLiteral(s_in, nocase_in, false, id_in, HWLM_ALL_GROUPS, {}, {}) {}
};

/**
 * Consistency test; returns false if the given msk/cmp test can never match
 * the literal string s.
 */
bool maskIsConsistent(const std::string &s, bool nocase,
                      const std::vector<u8> &msk, const std::vector<u8> &cmp);

} // namespace ue2

#endif // HWLM_LITERAL_H
