/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

/* Support for the Renesas RZ family of microprocessors */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexar.h"
#include "cortex.h"
#include "cortex_internal.h"
#include "spi.h"
#include "sfdp.h"

/*
 * Part numbering scheme
 *
 *  R7   S   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (S: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R7')
 *
 *  R9   A   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (A: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R9')
 *
 * Renesas Flash MCUs have an internal 16 byte read only register that stores
 * the part number, the code is stored ascii encoded, starting from the lowest memory address
 * except for pnrs stored in 'FIXED_PNR1', where the code is stored in reverse order (but the last 3 bytes are still 0x20 aka ' ')
 */

/* Base address for the OCRAM regions, including their mirrors (including RETRAM) */
#define RENESAS_OCRAM_BASE        0x20000000U
#define RENESAS_OCRAM_MIRROR_BASE 0x60000000U

/* Maximum known OCRAM size for the RZ series (10MB on the RZ/A1H?) */
#define RENESAS_OCRAM_MAX_SIZE 0x00a00000U

/* RAM scanning increment (512k default to speed things up) */
#define RENESAS_OCRAM_SCAN_INCREMENT 0x00080000U

/* Base address and max size for the SPI Flash XIP region */
#define RENESAS_SPI_FLASH_BASE 0x18000000U
#define RENESAS_SPI_FLASH_SIZE 0x04000000U

/* Base address for the boundary scan controller and boot mode register */
/*
 * NB: These addresses are only documented by rev 1 of the manual,
 * all further versions deleted these addresses and their documentation
 * wholesale. This has also been deduced in part from the ROM.
 */
#define RENESAS_BSCAN_BASE      0xfcfe1800U
#define RENESAS_BSCAN_BOOT_MODE (RENESAS_BSCAN_BASE + 0x000U)
#define RENESAS_BSCAN_BSID      (RENESAS_BSCAN_BASE + 0x004U)

#define RENESAS_BSCAN_BOOT_MODE_SPI  0x00000004U
#define RENESAS_BSCAN_BOOT_MODE_MASK 0x00000006U
#define RENESAS_BSCAN_BSID_RZ_A1L    0x081a6447U
#define RENESAS_BSCAN_BSID_RZ_A1LU   0x08178447U
#define RENESAS_BSCAN_BSID_RZ_A1LC   0x082f4447U

/* SPI Multi I/O Bus Controller registers, from R01UH0437EJ0600 §17.4, pg739 (17-4) */
#define RENESAS_MULTI_IO_SPI_BASE             0x3fefa000U
#define RENESAS_MULTI_IO_SPI_COMMON_CTRL      (RENESAS_MULTI_IO_SPI_BASE + 0x000U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL        (RENESAS_MULTI_IO_SPI_BASE + 0x00cU)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL        (RENESAS_MULTI_IO_SPI_BASE + 0x020U)
#define RENESAS_MULTI_IO_SPI_MODE_CMD         (RENESAS_MULTI_IO_SPI_BASE + 0x024U)
#define RENESAS_MULTI_IO_SPI_MODE_ADDR        (RENESAS_MULTI_IO_SPI_BASE + 0x028U)
#define RENESAS_MULTI_IO_SPI_MODE_DUMMY_DATA  (RENESAS_MULTI_IO_SPI_BASE + 0x02cU)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG (RENESAS_MULTI_IO_SPI_BASE + 0x030U)
#define RENESAS_MULTI_IO_SPI_MODE_READ_DATA   (RENESAS_MULTI_IO_SPI_BASE + 0x038U)
#define RENESAS_MULTI_IO_SPI_MODE_WRITE_DATA  (RENESAS_MULTI_IO_SPI_BASE + 0x040U)
#define RENESAS_MULTI_IO_SPI_MODE_STATUS      (RENESAS_MULTI_IO_SPI_BASE + 0x048U)

#define RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI             (1U << 31U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL_CS_UNSELECT            (1U << 24U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL_CACHE_FLUSH            (1U << 9U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD                (1U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_READ_ENABLE            (1U << 2U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_WRITE_ENABLE           (1U << 1U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_RUN_XFER               (1U << 0U)
#define RENESAS_MULTI_IO_SPI_MODE_CMD_SHIFT                   16U
#define RENESAS_MULTI_IO_SPI_MODE_CMD_MASK                    0x00ff0000U
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_CMD             (1U << 14U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_NONE       (0x0U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_3B         (0x7U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_4B         (0xfU << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DUMMY_SHIFT     4U
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT 0U
#define RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE        (1U << 0U)

/* ARM PL310 L2 cache controller registers, from DDI0246C §3.2, pg83 (3-5) */
#define ARM_PL310_BASE                        0x3ffff000U
#define ARM_PL310_CACHE_SYNC                  (ARM_PL310_BASE + 0x730U)
#define ARM_PL310_INVALIDATE_BY_WAY           (ARM_PL310_BASE + 0x77cU)
#define ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY (ARM_PL310_BASE + 0x7fcU)

#define RENESAS_ARM_PL310_CACHE_ASSOCIATIVITY 8U


/*
 * The boot ROM starts the loader from the start of SPI flash. The Renesas RZ/A1 sample loader then looks for an
 * application image at the offset below, copies it into RAM and jumps to the execute address in its header.
 */
#define RENESAS_APP_HEADER_BASE      (RENESAS_SPI_FLASH_BASE + 0x00080000U)
#define RENESAS_APP_HEADER_EXECUTE   (RENESAS_APP_HEADER_BASE + 0x028U)
#define RENESAS_APP_HEADER_SIGNATURE (RENESAS_APP_HEADER_BASE + 0x02cU)
/* First 4 bytes of the ".BootLoad_ValidProgramTest." signature */
#define RENESAS_APP_HEADER_SIGNATURE_VALUE 0x6f6f422eU

/* Watchdog timer registers, from R01UH0437EJ0700 §12.3 */
/* WDT registers are written as 16-bit values with a key in the upper byte to guard against accidental writes */
#define RENESAS_WDT_BASE  0xfcfe0000U
#define RENESAS_WDT_WTCSR (RENESAS_WDT_BASE + 0x000U)
#define RENESAS_WDT_WTCNT (RENESAS_WDT_BASE + 0x002U)
#define RENESAS_WDT_WRCSR (RENESAS_WDT_BASE + 0x004U)

#define RENESAS_WDT_WTCSR_MAGIC      0xa500U
#define RENESAS_WDT_WTCSR_WT         (1U << 6U)
#define RENESAS_WDT_WTCSR_TME        (1U << 5U)
#define RENESAS_WDT_WTCNT_MAGIC      0x5a00U
#define RENESAS_WDT_WTCNT_MAX		 0x00ffU /* Counter value that overflows on the next count, §12.5.2 */
#define RENESAS_WDT_WRCSR_MAGIC      0x5a00U
#define RENESAS_WDT_WRCSR_RSTE       (1U << 6U)
#define RENESAS_WDT_WRCSR_CLEAR_WOVF 0xa500U

/* This is the part number from the ROM table of a R7S721030 and is a guess */
#define ID_RZ_A1 0x012U

static const char *renesas_rz_part_name(uint32_t part_id);
static uint32_t renesas_rz_ram_size(target_s *target);
static bool renesas_rz_flash_prepare(target_s *target);
static bool renesas_rz_flash_resume(target_s *target);
static void renesas_rz_spi_read(target_s *target, uint16_t command, target_addr_t address, void *buffer, size_t length);
static void renesas_rz_spi_write(
	target_s *target, uint16_t command, target_addr_t address, const void *buffer, size_t length);
static void renesas_rz_spi_run_command(target_s *target, uint16_t command, target_addr_t address);
static void renesas_rz_reset(target_s *target);
static bool renesas_rz_cmd_reset_halt(target_s *target, int argc, const char **argv);

static const command_s renesas_rz_cmd_list[] = {
	{"reset_halt", renesas_rz_cmd_reset_halt, "Reset the SoC and halt the core at the application entry point"},
	{NULL, NULL, NULL},
};

static void renesas_rz_add_flash(target_s *const target)
{
	target->enter_flash_mode = renesas_rz_flash_prepare;
	target->exit_flash_mode = renesas_rz_flash_resume;

	/* Put the controller into manual SPI operations mode */
	renesas_rz_flash_prepare(target);
	/* Register the Flash via the SPI Flash implementation */
	bmp_spi_add_flash(target, RENESAS_SPI_FLASH_BASE, RENESAS_SPI_FLASH_SIZE, renesas_rz_spi_read, renesas_rz_spi_write,
		renesas_rz_spi_run_command);
	/* Put the controller back into bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) & ~RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);
}

bool renesas_rz_probe(target_s *const target)
{
	/* Determine that it's *probably* a RZ part */
	if (target->part_id != ID_RZ_A1)
		return false;

	/* Read out the BSID register to confirm that */
	const uint32_t part_id = target_mem32_read32(target, RENESAS_BSCAN_BSID);
	/* If the read failed, it's not a RZ/A1L* part */
	if (!part_id)
		return false;

	target->driver = renesas_rz_part_name(part_id);
	/*
	 * The generic Cortex-A/R reset pulses nRST, which also resets the debug logic, and then lets the core run
	 * straight into whatever firmware is present. Replace it with one that halts the core before the application runs.
	 * N.B. the bootloader still runs, and the renesas bootloader will overwrite the application from flash.
	 * If loading to ram it must be done after this reset
	 */
	target->reset = renesas_rz_reset;
	target_add_commands(target, renesas_rz_cmd_list, target->driver);

	/* Now determine the boot mode */
	const uint8_t boot_mode = target_mem32_read32(target, RENESAS_BSCAN_BOOT_MODE) & RENESAS_BSCAN_BOOT_MODE_MASK;
	if (boot_mode == RENESAS_BSCAN_BOOT_MODE_SPI)
		renesas_rz_add_flash(target);

	/* Determine RAM size by attempting to read in 512MB increments */
	const uint32_t ram_size = renesas_rz_ram_size(target);
	target_add_ram32(target, RENESAS_OCRAM_BASE, ram_size);
	target_add_ram32(target, RENESAS_OCRAM_MIRROR_BASE, ram_size);
	return true;
}

static const char *renesas_rz_part_name(const uint32_t part_id)
{
	switch (part_id) {
	case RENESAS_BSCAN_BSID_RZ_A1L:
		return "RZ/A1L";
	case RENESAS_BSCAN_BSID_RZ_A1LC:
		return "RZ/A1LC";
	case RENESAS_BSCAN_BSID_RZ_A1LU:
		/* This is common to A1LU and A1H at least */
		return "RZ/A1";
	}
	return "Unknown";
}

/*
 * Function to read RAM incrementally to determine the size of it for the given model.
 */
static uint32_t renesas_rz_ram_size(target_s *target)
{
	for (uint32_t addr = RENESAS_OCRAM_BASE; addr < RENESAS_OCRAM_BASE + RENESAS_OCRAM_MAX_SIZE;
		 addr += RENESAS_OCRAM_SCAN_INCREMENT) {
		uint32_t result;
		/* Read ahead by one scan increment and if there's an error, return the current size. */
		if (target_mem32_read(target, &result, addr + RENESAS_OCRAM_SCAN_INCREMENT - 8U, sizeof(result)))
			return addr - RENESAS_OCRAM_BASE;
	}
	return RENESAS_OCRAM_MAX_SIZE;
}

/*
 * Reset the whole SoC by making the watchdog overflow with its internal reset enabled. The internal reset is used as
 * the power-on reset (§12.1), but unlike nRST it leaves the debug logic alone. Leaves WRCSR.WOVF set (§12.5.5).
 */
static void renesas_rz_wdt_reset(target_s *const target)
{
	/* Stop the timer and clear any previous overflow, as WOVF being set blocks further resets (§12.5.5) */
	target_mem32_write16(target, RENESAS_WDT_WTCSR, RENESAS_WDT_WTCSR_MAGIC);
	target_mem32_read8(target, RENESAS_WDT_WRCSR);
	target_mem32_write16(target, RENESAS_WDT_WRCSR, RENESAS_WDT_WRCSR_CLEAR_WOVF);
	/*
	 * Enable reset on overflow, load WTCNT with H'FF (which counts as an immediate overflow, §12.5.2) and start the
	 * timer in watchdog mode, which resets the SoC straight away
	 */
	target_mem32_write16(target, RENESAS_WDT_WRCSR, RENESAS_WDT_WRCSR_MAGIC | RENESAS_WDT_WRCSR_RSTE);
	target_mem32_write16(target, RENESAS_WDT_WTCNT, RENESAS_WDT_WTCNT_MAGIC | RENESAS_WDT_WTCNT_MAX);
	target_mem32_write16(
		target, RENESAS_WDT_WTCSR, RENESAS_WDT_WTCSR_MAGIC | RENESAS_WDT_WTCSR_WT | RENESAS_WDT_WTCSR_TME);
}

/* Wait for the core to halt, then have halt_poll() set it up for debug and save its registers */
static void renesas_rz_wait_halt(target_s *const target, const uint32_t timeout_ms)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, timeout_ms);
	while (!(cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) & CORTEXAR_DBG_DSCR_HALTED) &&
		!platform_timeout_is_expired(&timeout))
		continue;
	target_halt_poll(target, NULL);
}

/* Resume the core and let it run until it reaches the given (ARM mode) address, using a hardware breakpoint */
static void renesas_rz_run_to(target_s *const target, const target_addr_t address)
{
	target_breakwatch_set(target, TARGET_BREAK_HARD, address, 4U);
	target_halt_resume(target, false);
	renesas_rz_wait_halt(target, 2000U);
	target_breakwatch_clear(target, TARGET_BREAK_HARD, address, 4U);
}

static void renesas_rz_run_to_app(target_s *const target)
{
	/* Run the boot ROM up to where it hands over to the loader, by which point it's set up the SPI flash window */
	renesas_rz_run_to(target, RENESAS_SPI_FLASH_BASE);
	/* Check there's a valid application image for the loader to start, so we don't run to a garbage address */
	if (target_mem32_read32(target, RENESAS_APP_HEADER_SIGNATURE) != RENESAS_APP_HEADER_SIGNATURE_VALUE)
		return;
	/* Then run the loader up to the application's entry point */
	renesas_rz_run_to(target, target_mem32_read32(target, RENESAS_APP_HEADER_EXECUTE));
}

/*
 * Reset the whole SoC via the watchdog and catch on the reset vector before the boot ROM gets a chance to run.
 * When booting from SPI flash, the boot ROM and loader are then allowed to run up to the application.
 * The reset is done via the watchdog rather than nRST. On the tested board nRST also resets the debug logic
 * and by the time it can be accessed again the core is already running.
 *
 * The alternative considered was to pulse nRST, bring the DP back up (JTAG-to-SWD switch and power-up request) and
 * then halt the core by request as soon as possible (which appears to be what J-Link's Cortex-A reset falls back to).
 * That seems to usually be midway through the loader. Resetting via the watchdog instead keeps the vector catch so
 * it'll always land on the reset vector, then run to the application entry once the loader has finished.
 */
static void renesas_rz_reset(target_s *const target)
{
	/*
	 * Make sure halting debug is enabled, as without it the vector catch falls to the abort handler
	 */
	const uint32_t dscr = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR);
	cortex_dbg_write32(target, CORTEXAR_DBG_DSCR, dscr | CORTEXAR_DBG_DSCR_HALTING_DBG_ENABLE);
	/* Arm the reset vector catch */
	const uint32_t vcr = cortex_dbg_read32(target, CORTEXAR_DBG_VCR);
	cortex_dbg_write32(target, CORTEXAR_DBG_VCR, vcr | CORTEXAR_DBG_VCR_RESET);
	/* Read PRSR to clear its sticky reset bit so we can tell when the reset below has actually happened */
	cortex_dbg_read32(target, CORTEXAR_DBG_PRSR);

	renesas_rz_wdt_reset(target);

	/*
	 * Wait for the reset to happen (sticky reset set) and complete (reset no longer active). The core was halted
	 * before the reset, so this must be seen before checking for it halting again on the reset vector.
	 */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 1000);
	bool reset_seen = false;
	while (!platform_timeout_is_expired(&timeout)) {
		const uint32_t prsr = cortex_dbg_read32(target, CORTEXAR_DBG_PRSR);
		reset_seen |= (prsr & CORTEXAR_DBG_PRSR_STICKY_RESET) != 0U;
		if (reset_seen && !(prsr & CORTEXAR_DBG_PRSR_RESET_ACTIVE))
			break;
	}

	/* Wait for the core to halt on the reset vector */
	renesas_rz_wait_halt(target, 250U);

	/* Disarm the vector catch so a reset initiated by the firmware (watchdog etc) does not halt the core */
	cortex_dbg_write32(target, CORTEXAR_DBG_VCR, vcr & ~CORTEXAR_DBG_VCR_RESET);

	/*
	 * When booting from SPI flash, let the boot ROM and loader run, stopping at the application's entry point.
	 * This leaves the hardware set up by the loader (which some firmware relies on), without running any of
	 * the application itself.
	 */
	const uint8_t boot_mode = target_mem32_read32(target, RENESAS_BSCAN_BOOT_MODE) & RENESAS_BSCAN_BOOT_MODE_MASK;
	if (boot_mode == RENESAS_BSCAN_BOOT_MODE_SPI)
		renesas_rz_run_to_app(target);
}

/*
 * Monitor command that does the same reset as GDB's run/start without killing the target.
 * IDE front ends treat a kill as ending the debug session and disconnect.
 * Should be followed by maintenance flush register-cache as gdb won't do that automatically
 */
static bool renesas_rz_cmd_reset_halt(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	target_reset(target);
	const bool halted = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) & CORTEXAR_DBG_DSCR_HALTED;
	uint32_t pc = 0U;
	target_reg_read(target, CORTEX_REG_PC, &pc, sizeof(pc));
	tc_printf(target, "Core %s after reset, PC = 0x%08" PRIx32 "\n", halted ? "halted" : "NOT halted", pc);
	return halted;
}

static bool renesas_rz_flash_prepare(target_s *const target)
{
	/* Halt any ongoing bust reads */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_READ_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL) | RENESAS_MULTI_IO_SPI_READ_CTRL_CS_UNSELECT);
	/* Wait for any existing operations to complete */
	while (!(
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_STATUS) & RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE))
		continue;
	/* Bring the controller out of bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) | RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);
	return true;
}

static bool renesas_rz_flash_resume(target_s *const target)
{
	/* Flush the controller's read cache */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_READ_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL) | RENESAS_MULTI_IO_SPI_READ_CTRL_CACHE_FLUSH);
	target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL);
	/* Put the controller back into bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) & ~RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);
	/* Invalidate the L1 D-caches and I-caches */
	cortexar_invalidate_all_caches(target);
	/* Invalidate the L2 cache ways so we get a clean state */
	const uint32_t l2_cache_ways_mask = (1U << RENESAS_ARM_PL310_CACHE_ASSOCIATIVITY) - 1U;
	target_mem32_write32(target, ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY, l2_cache_ways_mask);
	while (target_mem32_read32(target, ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY) & l2_cache_ways_mask)
		continue;
	target_mem32_write32(target, ARM_PL310_CACHE_SYNC, 0U);
	return true;
}

static uint32_t renesas_rz_spi_setup_xfer(
	target_s *const target, const uint16_t command, const target_addr_t address, const size_t length)
{
	/* Set up the command byte, dummy bytes and address for the transfer */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_CMD, opcode << RENESAS_MULTI_IO_SPI_MODE_CMD_SHIFT);
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_ADDR, address);
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_DUMMY_DATA, 0U);

	/* Set up the phases that need to be enabled for the transfer */
	uint32_t config = RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_CMD;
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR)
		config |= RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_3B;
	/* If we need to insert any dummy byte cycles into the transaction */
	const uint8_t dummy_bytes = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	if (dummy_bytes) {
		/*
		 * Create a bit mask for how much of the dummy data register to send to generate enough cycles.
		 * This will start out right aligned - eg, for 3 dummy cycles, 0b0111, so we need to then
		 * left align this within the nibble by shifting by 4 - dummy_bytes, producing a value like 0b1110.
		 * Finally we then shift this up into place within the config value.
		 * NB: This will not work for any more than 4 dummy byte cycles.
		 */
		config |= (((1U << dummy_bytes) - 1U) << (4U - dummy_bytes))
			<< RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DUMMY_SHIFT;
	}
	/* Create a bit mask for the first part of the required data transfer, same process as with the dummy bytes */
	const uint8_t initial_bytes = MIN(4U, length);
	config |= (((1U << initial_bytes) - 1U) << (4U - initial_bytes))
		<< RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT;
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG, config);

	/* If the transfer has no data associated with it, do not enable either transfer direction */
	if (!length)
		return 0;
	/*
	 * Convert the transaction direction into a control register direction setting and
	 * configure if we need ~CS held at the end of the transfer
	 */
	return ((command & SPI_FLASH_DATA_MASK) == SPI_FLASH_DATA_IN ? RENESAS_MULTI_IO_SPI_MODE_CTRL_READ_ENABLE :
																   RENESAS_MULTI_IO_SPI_MODE_CTRL_WRITE_ENABLE) |
		(length > 4U ? RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD : 0U);
}

static void renesas_rz_spi_run_xfer(target_s *const target, const uint32_t ctrl)
{
	/* Set the requested transfer running */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_CTRL, ctrl | RENESAS_MULTI_IO_SPI_MODE_CTRL_RUN_XFER);
	/* Wait for it to complete */
	while (!(
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_STATUS) & RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE))
		continue;
}

static void renesas_rz_spi_read(target_s *const target, const uint16_t command, const target_addr_t address,
	void *const buffer, const size_t length)
{
	/* Set up the transaction */
	uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, length);
	uint8_t *const data = (uint8_t *)buffer;
	/* For each 4 byte chunk to be read */
	for (size_t offset = 0U; offset < length;) {
		/* Run the transfer that's configured */
		renesas_rz_spi_run_xfer(target, ctrl);
		/* Read back the data read and copy it into the output buffer */
		const uint32_t value = target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_READ_DATA);
		memcpy(data + offset, &value, MIN(length - offset, 4U));
		/* Turn off all the optional phases and set up the next transfer chunk */
		offset += 4U;
		const uint8_t amount = MIN(4U, length - offset);
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG,
			(((1U << amount) - 1U) << (4U - amount)) << RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT);
		/* Adjust the control value if we're going into the last read of the block */
		if (length - offset <= 4U)
			ctrl &= ~RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD;
	}
}

static void renesas_rz_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Set up the transaction */
	uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, length);
	const uint8_t *const data = (const uint8_t *)buffer;
	/* For each 4 byte chunk to be written */
	for (size_t offset = 0U; offset < length;) {
		/* Prepare the data to send from the input buffer and write it to the target */
		uint32_t value = 0U;
		memcpy(&value, data + offset, MIN(length - offset, 4U));
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_WRITE_DATA, value);
		/* Run the transfer that's configured */
		renesas_rz_spi_run_xfer(target, ctrl);
		/* Turn off all the optional phases and set up the next transfer chunk */
		offset += 4U;
		const uint8_t amount = MIN(4U, length - offset);
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG,
			(((1U << amount) - 1U) << (4U - amount)) << RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT);
		/* Adjust the control value if we're going into the last write of the block */
		if (length - offset <= 4U)
			ctrl &= ~RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD;
	}
}

static void renesas_rz_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	/* Set up and run the requested command transaction */
	const uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, 0U);
	renesas_rz_spi_run_xfer(target, ctrl);
}
