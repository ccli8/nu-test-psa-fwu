/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "psa/update.h"

#define TEST_INTERACT       1   // Interactive test
#define TEST_STRESS         0   // Stress test

static const uint8_t update_firmware_data[] = {
    #include "update_firmware/update_firmware.hex"
};

static psa_image_id_t image_id_ns_active = \
    (psa_image_id_t) FWU_CALCULATE_IMAGE_ID(FWU_IMAGE_ID_SLOT_ACTIVE,
                                            FWU_IMAGE_TYPE_NONSECURE,
                                            0);
static psa_image_id_t image_id_ns_staging = \
    (psa_image_id_t) FWU_CALCULATE_IMAGE_ID(FWU_IMAGE_ID_SLOT_STAGE,
                                            FWU_IMAGE_TYPE_NONSECURE,
                                            0);

static const char *psa_image_status_arr[] = {
    "PSA_IMAGE_UNDEFINED",
    "PSA_IMAGE_CANDIDATE",
    "PSA_IMAGE_INSTALLED",
    "PSA_IMAGE_REJECTED",
    "PSA_IMAGE_PENDING_INSTALL",
    "PSA_IMAGE_REBOOT_NEEDED",
};

#define MAX_BLOCK_SIZE      752 //PSA_FWU_MAX_BLOCK_SIZE

static void interact_test(void);
static void stress_test(uint32_t rounds);

static void show_image_info(void);
static bool write_update_firmware(void);
static bool install_update_firmware(void);
static bool abort_update_firmware(void);
static void reboot(Kernel::Clock::duration_u32 rel_time);

int main()
{
#if TEST_INTERACT
    interact_test();
#elif TEST_STRESS
    stress_test(500);
#endif

    return 0;
}

static void interact_test(void)
{
    while (1) {
        printf("Press key\r\n"
               "a) for aborting update firmware\r\n"
               "i) for image information\r\n"
               "p) for prompt\r\n"
               "r) for system reboot\r\n"
               "s) for installing update firmware\r\n"
               "w) for writing update firmware\r\n"
               "x) for exit\r\n");
        int in_char = getchar();
        if (in_char == 'a') {
            printf("\r\nAborting update firmware...\n\n");
            abort_update_firmware();
        } else if (in_char == 'i') {
            printf("\r\nShowing image information...\n\n");
            show_image_info();
        } else if (in_char == 'p') {
            continue;
        } else if (in_char == 'r') {
            printf("\r\nSystem is going to reboot after 3s...\n\n");
            reboot(3s);
        } else if (in_char == 's') {
            printf("\r\nInstalling update firmware...\n\n");
            install_update_firmware();
        } else if (in_char == 'w') {
            printf("\r\nWriting update firmware...\r\n");
            write_update_firmware();
        } else if (in_char == 'x') {
            printf("\r\nEscaped from firmware update\r\n");
            break;
        }
    }
}

static void stress_test(uint32_t rounds)
{
    uint32_t i = 0;
    
    for (; i < rounds; i ++) {
        printf("Round (%d/%d)...\r\n", i + 1, rounds);
        if (!write_update_firmware()) {
            printf("write_update_firmware() failed\r\n");
            return;
        }
        if (!abort_update_firmware()) {
            printf("abort_update_firmware() failed\r\n");
            return;
        }
        printf("Round (%d/%d)...OK\r\n", i + 1, rounds);
    }
    
    printf("\r\nSystem is going to reboot after 3s...\n\n");
    reboot(3s);
}

static void show_image_info(void)
{
    psa_status_t status;
    psa_image_info_t info;

    /* Query the NS active image */
    status = psa_fwu_query(image_id_ns_active, &info);
    if (status != PSA_SUCCESS) {
        printf("Query NS active image: %d\r\n", status);
    } else {
        printf("NS active image: state=%s, version=%d.%d.%d+%d\r\n",
               psa_image_status_arr[info.state], info.version.iv_major,
               info.version.iv_minor, info.version.iv_revision,
               info.version.iv_build_num);
    }

    /* Query the NS staging image */
    status = psa_fwu_query(image_id_ns_staging, &info);
    if (status != PSA_SUCCESS) {
        printf("Query NS staging image: %d\r\n", status);
    } else {
        printf("NS staging image: state=%s, version=%d.%d.%d+%d\r\n",
               psa_image_status_arr[info.state], info.version.iv_major,
               info.version.iv_minor, info.version.iv_revision,
               info.version.iv_build_num);
    }
}

static bool write_update_firmware(void)
{
    psa_status_t status;
    psa_image_info_t info;
    const uint8_t *fwu_src_beg = update_firmware_data;
    const uint8_t *fwu_src_pos = update_firmware_data;
    const uint8_t *fwu_src_end = update_firmware_data + sizeof(update_firmware_data);
    size_t fwu_dst_pos = 0;
    size_t fwu_todo;

    /* Write to NS staging area */
    while (fwu_src_pos < fwu_src_end) {
        fwu_todo = fwu_src_end - fwu_src_pos;
        if (fwu_todo > MAX_BLOCK_SIZE) {
            fwu_todo = MAX_BLOCK_SIZE;
        }

        status = psa_fwu_write(image_id_ns_staging, fwu_dst_pos,
                               fwu_src_pos, fwu_todo);
        if (status != PSA_SUCCESS) {
            printf("psa_fwu_write() failed: %d\r\n", status);
            return false;
        }

        /* Print progress */
        printf("\r%d/%d (bytes) completed", fwu_src_pos - fwu_src_beg,
               fwu_src_end - fwu_src_beg);

        /* Next block */
        fwu_dst_pos += fwu_todo;
        fwu_src_pos += fwu_todo;
    }
    
    /* Print progress */
    printf("\r%d/%d (bytes) completed\r\n", fwu_src_pos - fwu_src_beg,
            fwu_src_end - fwu_src_beg);

    /* Check if the NS staging image's state is PSA_IMAGE_CANDIDATE */
    status = psa_fwu_query(image_id_ns_staging, &info);
    if (status != PSA_SUCCESS) {
        printf("Query NS staging image: %d\r\n", status);
        return false;
    } else if (info.state != PSA_IMAGE_CANDIDATE) {
        printf("NS staging image's state should be PSA_IMAGE_CANDIDATE instead of %s"
               "after successful write\r\n", psa_image_status_arr[info.state]);
        return false;
    }
    
    return true;
}

static bool install_update_firmware(void)
{
    psa_status_t status;
    psa_image_info_t info;
    psa_image_id_t dependency_uuid;
    psa_image_version_t dependency_version;

    status = psa_fwu_install(image_id_ns_staging, &dependency_uuid, &dependency_version);
    /* In the currently implementation, image verification is deferred to
     * reboot, so PSA_SUCCESS_REBOOT is returned when success.
     */
    if (status != PSA_SUCCESS_REBOOT) {
        printf("psa_fwu_install() failed: %d\r\n", status);
        return false;
    }

    /* Check if the NS staging image's state is PSA_IMAGE_REBOOT_NEEDED */
    status = psa_fwu_query(image_id_ns_staging, &info);
    if (status != PSA_SUCCESS) {
        printf("Query NS staging image: %d\r\n", status);
        return false;
    } else if (info.state != PSA_IMAGE_REBOOT_NEEDED) {
        printf("NS staging image's state should be PSA_IMAGE_REBOOT_NEEDED instead of %s"
               "after successful write\r\n", psa_image_status_arr[info.state]);
        return false;
    }
    
    return true;
}

static bool abort_update_firmware(void)
{
    psa_status_t status;
    psa_image_info_t info;

    /* Abort the update firmware */
    status = psa_fwu_abort(image_id_ns_staging);
    if (status != PSA_SUCCESS) {
        printf("psa_fwu_abort() failed with %d\r\n", status);
        return false;
    }

    /* Check if the NS staging image's state is PSA_IMAGE_UNDEFINED */
    status = psa_fwu_query(image_id_ns_staging, &info);
    if (status != PSA_SUCCESS) {
        printf("Query NS staging image: %d\r\n", status);
        return false;
    } else if (info.state != PSA_IMAGE_UNDEFINED) {
        printf("NS staging image's state should be PSA_IMAGE_UNDEFINED instead of %s"
               "after successful write\r\n", psa_image_status_arr[info.state]);
        return false;
    }
    
    return true;
}

static void reboot(Kernel::Clock::duration_u32 rel_time)
{
    ThisThread::sleep_for(rel_time);
    //NVIC_SystemReset();
    psa_fwu_request_reboot();
}
