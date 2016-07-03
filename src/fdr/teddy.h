/*
 * Copyright (c) 2016, Intel Corporation
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
 * \brief Teddy literal matcher: function declarations.
 */

#ifndef TEDDY_H_
#define TEDDY_H_

struct FDR; // forward declaration from fdr_internal.h
struct FDR_Runtime_Args;

hwlm_error_t fdr_exec_teddy_msks1(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks1_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks2(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks2_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks3(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks3_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks4(const struct FDR *fdr,
                                  const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_msks4_pck(const struct FDR *fdr,
                                      const struct FDR_Runtime_Args *a);

#if defined(__AVX2__)

hwlm_error_t fdr_exec_teddy_avx2_msks1_fat(const struct FDR *fdr,
                                           const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks1_pck_fat(const struct FDR *fdr,
                                               const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks2_fat(const struct FDR *fdr,
                                           const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks2_pck_fat(const struct FDR *fdr,
                                               const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks3_fat(const struct FDR *fdr,
                                           const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks3_pck_fat(const struct FDR *fdr,
                                               const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks4_fat(const struct FDR *fdr,
                                           const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks4_pck_fat(const struct FDR *fdr,
                                               const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks1_fast(const struct FDR *fdr,
                                            const struct FDR_Runtime_Args *a);

hwlm_error_t fdr_exec_teddy_avx2_msks1_pck_fast(const struct FDR *fdr,
                                                const struct FDR_Runtime_Args *a);

#endif /* __AVX2__ */

#endif /* TEDDY_H_ */
