/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TARGET_CORTEXAR_H
#define TARGET_CORTEXAR_H

#include "general.h"

bool cortexar_attach(target_s *target);
void cortexar_detach(target_s *target);
void cortexar_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len);
void cortexar_mem_write(target_s *target, target_addr64_t dest, const void *src, size_t len);
void cortexar_invalidate_all_caches(target_s *target);

#define CORTEXAR_DBG_IDR   0x000U /* ID register */
#define CORTEXAR_DBG_WFAR  0x018U
#define CORTEXAR_DBG_VCR   0x01cU /* Vector Catch register */
#define CORTEXAR_DBG_DSCCR 0x028U
#define CORTEXAR_DBG_DTRTX 0x080U /* DBGDTRRXext */
#define CORTEXAR_DBG_ITR   0x084U /* Instruction register */
#define CORTEXAR_DBG_DSCR  0x088U /* Debug Status and Control register */
#define CORTEXAR_DBG_DTRRX 0x08cU /* DBGDTRTXext */
#define CORTEXAR_DBG_DRCR  0x090U /* Debug Run Control register */
#define CORTEXAR_DBG_BVR   0x100U
#define CORTEXAR_DBG_BCR   0x140U
#define CORTEXAR_DBG_WVR   0x180U
#define CORTEXAR_DBG_WCR   0x1c0U
#define CORTEXAR_DBG_OSLAR 0x300U /* OS Lock Access register */
#define CORTEXAR_DBG_OSLSR 0x304U /* OS Lock Status register */
#define CORTEXAR_DBG_OSSRR 0x308U /* OS Save/Restore register */
#define CORTEXAR_DBG_OSDLR 0x30cU /* OS Double-Lock register */
#define CORTEXAR_DBG_PRCR  0x310U /* Power and Reset Control register */
#define CORTEXAR_DBG_PRSR  0x314U /* Power and Reset Status register */

#define CORTEXAR_CPUID 0xd00U
#define CORTEXAR_CTR   0xd04U
#define CORTEXAR_PFR1  0xd24U
#define CORTEXAR_MMFR0 0xd30U

#define CORTEXAR_DBG_IDR_BREAKPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_BREAKPOINT_SHIFT 24U
#define CORTEXAR_DBG_IDR_WATCHPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_WATCHPOINT_SHIFT 28U

#define CORTEXAR_DBG_VCR_RESET				 (1U << 0U)

#define CORTEXAR_DBG_DSCR_HALTED             (1U << 0U)
#define CORTEXAR_DBG_DSCR_RESTARTED          (1U << 1U)
#define CORTEXAR_DBG_DSCR_MOE_MASK           0x0000003cU
#define CORTEXAR_DBG_DSCR_MOE_HALT_REQUEST   0x00000000U
#define CORTEXAR_DBG_DSCR_MOE_BREAKPOINT     0x00000004U
#define CORTEXAR_DBG_DSCR_MOE_ASYNC_WATCH    0x00000008U
#define CORTEXAR_DBG_DSCR_MOE_BKPT_INSN      0x0000000cU
#define CORTEXAR_DBG_DSCR_MOE_EXTERNAL_DBG   0x00000010U
#define CORTEXAR_DBG_DSCR_MOE_VEC_CATCH      0x00000014U
#define CORTEXAR_DBG_DSCR_MOE_SYNC_WATCH     0x00000028U
#define CORTEXAR_DBG_DSCR_SYNC_DATA_ABORT    (1U << 6U)
#define CORTEXAR_DBG_DSCR_INTERRUPT_DISABLE  (1U << 11U)
#define CORTEXAR_DBG_DSCR_ITR_ENABLE         (1U << 13U)
#define CORTEXAR_DBG_DSCR_HALTING_DBG_ENABLE (1U << 14U)
#define CORTEXAR_DBG_DCSR_DCC_MASK           0x00300000U
#define CORTEXAR_DBG_DCSR_DCC_NORMAL         0x00000000U
#define CORTEXAR_DBG_DCSR_DCC_STALL          0x00100000U
#define CORTEXAR_DBG_DCSR_DCC_FAST           0x00200000U
#define CORTEXAR_DBG_DSCR_INSN_COMPLETE      (1U << 24U)
#define CORTEXAR_DBG_DSCR_DTR_READ_READY     (1U << 29U)
#define CORTEXAR_DBG_DSCR_DTR_WRITE_DONE     (1U << 30U)

#define CORTEXAR_DBG_DRCR_HALT_REQ           (1U << 0U)
#define CORTEXAR_DBG_DRCR_RESTART_REQ        (1U << 1U)
#define CORTEXAR_DBG_DRCR_CLR_STICKY_EXC     (1U << 2U)
#define CORTEXAR_DBG_DRCR_CLR_STICKY_PIPEADV (1U << 3U)
#define CORTEXAR_DBG_DRCR_CANCEL_BUS_REQ     (1U << 4U)

#define CORTEXAR_DBG_BCR_ENABLE                      0x00000001U
#define CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MATCH    0x00000000U
#define CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MISMATCH 0x00400000U
#define CORTEXAR_DBG_BCR_ALL_MODES                   0x00002006U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_ALL             0x000001e0U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_LOW_HALF        0x00000060U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_HIGH_HALF       0x00000180U

#define CORTEXAR_DBG_WCR_ENABLE             0x00000001U
#define CORTEXAR_DBG_WCR_MATCH_ON_LOAD      0x00000008U
#define CORTEXAR_DBG_WCR_MATCH_ON_STORE     0x00000010U
#define CORTEXAR_DBG_WCR_MATCH_ANY_ACCESS   0x00000018U
#define CORTEXAR_DBG_WCR_ALL_MODES          0x00002006U
#define CORTEXAR_DBG_WCR_BYTE_SELECT_OFFSET 5U
#define CORTEXAR_DBG_WCR_BYTE_SELECT_MASK   0x00001fe0U
#define CORTEXAR_DBG_WCR_BYTE_SELECT(x) \
	(((x) << CORTEXAR_DBG_WCR_BYTE_SELECT_OFFSET) & CORTEXAR_DBG_WCR_BYTE_SELECT_MASK)

#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL         0x00000009U
#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_FULL    0x00000001U
#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_PARTIAL 0x00000008U
#define CORTEXAR_DBG_OSLSR_LOCKED                (1U << 1U)

#define CORTEXAR_DBG_PRCR_CORE_POWER_DOWN_REQ  (1U << 0U)
#define CORTEXAR_DBG_PRCR_CORE_WARM_RESET_REQ  (1U << 1U)
#define CORTEXAR_DBG_PRCR_HOLD_CORE_WARM_RESET (1U << 2U)
#define CORTEXAR_DBG_PRCR_CORE_POWER_UP_REQ    (1U << 3U)

#define CORTEXAR_DBG_PRSR_POWERED_UP   (1U << 0U)
#define CORTEXAR_DBG_PRSR_STICKY_PD    (1U << 1U)
#define CORTEXAR_DBG_PRSR_RESET_ACTIVE (1U << 2U)
#define CORTEXAR_DBG_PRSR_STICKY_RESET (1U << 3U)
#define CORTEXAR_DBG_PRSR_HALTED       (1U << 4U)
#define CORTEXAR_DBG_PRSR_OS_LOCK      (1U << 5U)
#define CORTEXAR_DBG_PRSR_DOUBLE_LOCK  (1U << 6U)


#endif /* TARGET_CORTEXAR_H */
