/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2017-2020 Arm Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "hal_data.h"
#include "comms/comms.h"
#include "menu.h"
#include "header.h"
#include "crc16.h"
#if 0   //WangJin 2021.04.13
#include "bl2_util.h"
#include "target.h"
//#include "tfm_hal_device_header.h"
#include "Driver_Flash.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "flash_map_backend/flash_map_backend.h"
#include "boot_record.h"
#include "security_cnt.h"
#include "boot_hal.h"
#include "memory_buffer_alloc.h"
#endif
#if MCUBOOT_LOG_LEVEL > MCUBOOT_LOG_LEVEL_OFF
#include "uart_stdout.h"
#endif
#if defined(CRYPTO_HW_ACCELERATOR) || \
    defined(CRYPTO_HW_ACCELERATOR_OTP_PROVISIONING)
#include "crypto_hw.h"
#endif

/* Avoids the semihosting issue */
#if defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__asm("  .global __ARM_use_no_argv\n");
#endif

#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
/* Macros to pick linker symbols */
#define REGION(a, b, c) a##b##c
#define REGION_NAME(a, b, c) REGION(a, b, c)
#define REGION_DECLARE(a, b, c) extern uint32_t REGION_NAME(a, b, c)

REGION_DECLARE(Image$$, ARM_LIB_STACK, $$ZI$$Base);
#endif

//WangJin 2021.02.23
//#define   __ARM_LIB_STACK_start__   Image$$ARM_LIB_STACK$$ZI$$Base
#define   Image$$ARM_LIB_STACK$$ZI$$Base    "__ARM_LIB_STACK_start__"
//WangJin 2021.02.23

/* Flash device name must be specified by target */
//extern ARM_DRIVER_FLASH FLASH_DEV_NAME;

#define BL2_MBEDTLS_MEM_BUF_LEN 0x2000
/* Static buffer to be used by mbedtls for memory allocation */
//static uint8_t mbedtls_mem_buf[BL2_MBEDTLS_MEM_BUF_LEN];

struct arm_vector_table {
    uint32_t msp;
    uint32_t reset;
};

struct boot_rsp {
    /** A pointer to the header of the image to be executed. */
    const struct image_header *br_hdr;

    /**
     * The flash offset of the image to execute.  Indicates the position of
     * the image header within its flash device.
     */
    uint8_t br_flash_dev_id;
    uint32_t br_image_off;
};

int bl2_main(void);
void do_boot(struct boot_rsp *rsp);
void boot_jump_to_next_image(uint32_t reset_handler_addr);

/*!
 * \brief Chain-loading the next image in the boot sequence.
 *
 * This function calls the Reset_Handler of the next image in the boot sequence,
 * usually it is the secure firmware. Before passing the execution to next image
 * there is conditional rule to remove the secrets from the memory. This must be
 * done if the following conditions are satisfied:
 *  - Memory is shared between SW components at different stages of the trusted
 *    boot process.
 *  - There are secrets in the memory: KDF parameter, symmetric key,
 *    manufacturer sensitive code/data, etc.
 */
#if 0   //WangJin 2021.04.13
__attribute__((naked)) void boot_jump_to_next_image(uint32_t reset_handler_addr)
{
    __ASM volatile(
        ".syntax unified                 \n"
        "mov     r7, r0                  \n"
        "bl      boot_clear_bl2_ram_area \n" /* Clear RAM before jump */
        "movs    r0, #0                  \n" /* Clear registers: R0-R12, */
        "mov     r1, r0                  \n" /* except R7 */
        "mov     r2, r0                  \n"
        "mov     r3, r0                  \n"
        "mov     r4, r0                  \n"
        "mov     r5, r0                  \n"
        "mov     r6, r0                  \n"
        "mov     r8, r0                  \n"
        "mov     r9, r0                  \n"
        "mov     r10, r0                 \n"
        "mov     r11, r0                 \n"
        "mov     r12, r0                 \n"
        "mov     lr,  r0                 \n"
        "bx      r7                      \n" /* Jump to Reset_handler */
    );
}
#endif

void do_boot(struct boot_rsp *rsp)
{
    /* Clang at O0, stores variables on the stack with SP relative addressing.
     * When manually set the SP then the place of reset vector is lost.
     * Static variables are stored in 'data' or 'bss' section, change of SP has
     * no effect on them.
     */
    static struct arm_vector_table vt;
    uintptr_t flash_base;
    int rc;
    char dbg[80];

    (void)rsp;

    flash_base = APP_IMAGE_START_ADDRESS;

    /* Read app's vector table from flash into static RAM variable */
    vt.msp   = *(uint32_t *)(flash_base);
    vt.reset = *(uint32_t *)(flash_base + 4);

    /* Diagnostic output before jumping */
    snprintf(dbg, sizeof(dbg), "\r\n[do_boot] flash_base=0x%08X\r\n", (unsigned int)flash_base);
    comms_send((uint8_t *)dbg, strlen(dbg));
    snprintf(dbg, sizeof(dbg), "[do_boot] vt.msp=0x%08X  vt.reset=0x%08X\r\n", (unsigned int)vt.msp, (unsigned int)vt.reset);
    comms_send((uint8_t *)dbg, strlen(dbg));

    /* Dump first 32 bytes of App flash for verification */
    snprintf(dbg, sizeof(dbg), "[do_boot] App flash @0x%08X:", (unsigned int)flash_base);
    comms_send((uint8_t *)dbg, strlen(dbg));
    {
        int i;
        for (i = 0; i < 32; i++) {
            snprintf(dbg, sizeof(dbg), " %02X", *((uint8_t *)(flash_base + i)));
            comms_send((uint8_t *)dbg, strlen(dbg));
        }
    }
    comms_send((uint8_t *)"\r\n", 2);

    /* Close UART, timer and IOPORT before jumping to App */
    snprintf(dbg, sizeof(dbg), "[do_boot] Closing UART, Timer, IOPORT, then jumping to App...\r\n");
    comms_send((uint8_t *)dbg, strlen(dbg));
    g_uart0.p_api->close(g_uart0.p_ctrl);
    g_timer0.p_api->close(g_timer0.p_ctrl);
    R_IOPORT_Close(&g_ioport_ctrl);

#if 0   //WangJin 2021.04.13
    /* The beginning of the image is the ARM vector table, containing
     * the initial stack pointer address and the reset vector
     * consecutively. Manually set the stack pointer and jump into the
     * reset vector
     */
    rc = flash_device_base(rsp->br_flash_dev_id, &flash_base);
    assert(rc == 0);

    if (rsp->br_hdr->ih_flags & IMAGE_F_RAM_LOAD) {
       /* The image has been copied to SRAM, find the vector table
        * at the load address instead of image's address in flash
        */
        vt = (struct arm_vector_table *)(rsp->br_hdr->ih_load_addr +
                                         rsp->br_hdr->ih_hdr_size);
    } else {
        /* Using the flash address as not executing in SRAM */
        vt = (struct arm_vector_table *)(flash_base +
                                         rsp->br_image_off +
                                         rsp->br_hdr->ih_hdr_size);
    }

    rc = FLASH_DEV_NAME.Uninitialize();
    if(rc != ARM_DRIVER_OK) {
        BOOT_LOG_ERR("Error while uninitializing Flash Interface");
    }
#endif

#if MCUBOOT_LOG_LEVEL > MCUBOOT_LOG_LEVEL_OFF
    stdio_uninit();
#endif

#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
    /* Restore the Main Stack Pointer Limit register's reset value
     * before passing execution to runtime firmware to make the
     * bootloader transparent to it.
     */
    __set_MSPLIM(0);
#endif
#if defined(__ARMCC_VERSION)
    __set_MSP(vt.msp);
    __DSB();
    __ISB();

    boot_jump_to_next_image(vt.reset);
#else
    SCB->VTOR = (flash_base & 0x1FFFFF80);
    __DSB();

    /* Disable MSP monitoring  */
#if BSP_FEATURE_BSP_HAS_SP_MON    //Added WangJin 2021.02.22
    R_MPU_SPMON->SP[0].CTL = 0;
    while(R_MPU_SPMON->SP[0].CTL != 0);
#endif

    __set_MSP(vt.msp);

    ((void (*)()) vt.reset)();
#endif
}

int bl2_main(void)
{
#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
    uint32_t msp_stack_bottom =
            (uint32_t)&REGION_NAME(Image$$, ARM_LIB_STACK, $$ZI$$Base);
#endif

    struct boot_rsp rsp;
//    int rc = ARM_DRIVER_OK;;

#if defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
    __set_MSPLIM(msp_stack_bottom);
#endif

#if MCUBOOT_LOG_LEVEL > MCUBOOT_LOG_LEVEL_OFF
    stdio_init();
#endif

    uint8_t str[100] = {0};
    fsp_err_t err;

    err = comms_open();
    if ( FSP_SUCCESS != err ){
        while(1){
            ;
        }
    }

    err = g_flash0.p_api->open(g_flash0.p_ctrl, g_flash0.p_cfg);
    if ( FSP_SUCCESS != err ){
        snprintf((char *)str, sizeof(str), "ERROR: Opening flash driver failed!\r\n");
        comms_send(str, strlen((char *)str));
    }

    snprintf((char *)str, sizeof(str), "\r\n*** FW Ver.: 1.5 old***\r\n");
    comms_send(str, strlen((char *)str));

    /* Check if a valid App image exists, jump directly if OTA flag is set */
    uint32_t flag_end = *(uint32_t *)OTA_FLAG_END_ADDRESS;
    if( 0x55555555 == flag_end ){
        uint32_t CRC_rom = calcrc(APP_IMAGE_START_ADDRESS, (CRC_ADDRESS - APP_IMAGE_START_ADDRESS));
        uint16_t CRC_ccitt = (unsigned short)(*(unsigned int *)CRC_ADDRESS);
        if( CRC_rom == CRC_ccitt ){
            snprintf((char *)str, sizeof(str), "OTA flag set, CRC OK...Jumping to App\r\n");
            comms_send(str, strlen((char *)str));
            do_boot(&rsp);
        }
        else{
            snprintf((char *)str, sizeof(str), "OTA flag set but CRC NG (%04X != %04X), entering menu\r\n", CRC_rom, CRC_ccitt);
            comms_send(str, strlen((char *)str));
        }
    }

//    BOOT_LOG_INF("Starting bootloader");
    menu();

#if 0   //WangJin 2021.04.13
    /* Perform platform specific initialization */
    if (boot_platform_init() != 0) {
        while (1)
            ;
    }
    /* Initialise the mbedtls static memory allocator so that mbedtls allocates
     * memory from the provided static buffer instead of from the heap.
     */
    mbedtls_memory_buffer_alloc_init(mbedtls_mem_buf, BL2_MBEDTLS_MEM_BUF_LEN);

#ifdef CRYPTO_HW_ACCELERATOR
    rc = crypto_hw_accelerator_init();
    if (rc) {
        BOOT_LOG_ERR("Error while initializing cryptographic accelerator.");
        while (1);
    }
#endif /* CRYPTO_HW_ACCELERATOR */

#ifndef MCUBOOT_USE_UPSTREAM
    rc = boot_nv_security_counter_init();
    if (rc != 0) {
        BOOT_LOG_ERR("Error while initializing the security counter");
        while (1)
            ;
    }
#endif /* !MCUBOOT_USE_UPSTREAM */

    rc = boot_go(&rsp);
    if (rc != 0) {
        BOOT_LOG_ERR("Unable to find bootable image");
        while (1)
            ;
    }

#ifdef CRYPTO_HW_ACCELERATOR
    rc = crypto_hw_accelerator_finish();
    if (rc) {
        BOOT_LOG_ERR("Error while uninitializing cryptographic accelerator.");
        while (1);
    }
#endif /* CRYPTO_HW_ACCELERATOR */

/* This is a workaround to program the TF-M related cryptographic keys
 * to CC312 OTP memory. This functionality is independent from secure boot,
 * this is usually done in the factory floor during chip manufacturing.
 */
#ifdef CRYPTO_HW_ACCELERATOR_OTP_PROVISIONING
    BOOT_LOG_INF("OTP provisioning started.");
    rc = crypto_hw_accelerator_otp_provisioning();
    if (rc) {
        BOOT_LOG_ERR("OTP provisioning FAILED: 0x%X", rc);
        while (1);
    } else {
        BOOT_LOG_INF("OTP provisioning succeeded. TF-M won't be loaded.");

        /* We don't need to boot - the only aim is provisioning. */
        while (1);
    }
#endif /* CRYPTO_HW_ACCELERATOR_OTP_PROVISIONING */

    BOOT_LOG_INF("Bootloader chainload address offset: 0x%x",
                 rsp.br_image_off);
    BOOT_LOG_INF("Jumping to the first image slot");
#endif
    do_boot(&rsp);

    return 0;   //Should never be here
//    BOOT_LOG_ERR("Never should get here");
//    menu();
}
