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
 * \brief Multibit: build code (for sparse iterators)
 */

#ifndef MULTIBIT_BUILD_H
#define MULTIBIT_BUILD_H

#include "multibit_internal.h"

#include <vector>

/** \brief Comparator for \ref mmbit_sparse_iter structures. */
static inline
bool operator<(const mmbit_sparse_iter &a, const mmbit_sparse_iter &b) {
    if (a.mask != b.mask) {
        return a.mask < b.mask;
    }
    return a.val < b.val;
}

namespace ue2 {

/** \brief Construct a sparse iterator over the values in \a bits for a
 * multibit of size \a total_bits. */
void mmbBuildSparseIterator(std::vector<mmbit_sparse_iter> &out,
                            const std::vector<u32> &bits, u32 total_bits);

struct scatter_plan_raw;

void mmbBuildInitRangePlan(u32 total_bits, u32 begin, u32 end,
                           scatter_plan_raw *out);
void mmbBuildClearPlan(u32 total_bits, scatter_plan_raw *out);

} // namespace ue2

#endif // MULTIBIT_BUILD_H
