#include "mbed.h"

#include "mbed_trace.h"
#include "FlashIAPBlockDevice.h"
#include "block_device_application.hpp"
#include "candidate_applications.hpp"
#include <cstdint>

#if MBED_CONF_MBED_TRACE_ENABLE
#define TRACE_GROUP "bootloader"
#endif // MBED_CONF_MBED_TRACE_ENABLE

#if MBED_CONF_MBED_TRACE_ENABLE
static UnbufferedSerial g_uart(CONSOLE_TX, CONSOLE_RX);

// Function that directly outputs to an unbuffered serial port in blocking mode.
static void boot_debug(const char *s)
{
    size_t len = strlen(s);
    g_uart.write(s, len);
    g_uart.write("\r\n", 2);
}
#endif

int main()
{
#if MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();
    mbed_trace_print_function_set(boot_debug);
#endif // MBED_CONF_MBED_TRACE_ENABLE

    tr_debug("BikeComputer bootloader\r\n");

    FlashIAPBlockDevice flashIAPBlockDevice(MBED_ROM_START, MBED_ROM_SIZE);

    if(flashIAPBlockDevice.init() == 0)
    {
        // addresses are specified relative to block device base address
        mbed::bd_addr_t headerAddress      = HEADER_ADDR - MBED_ROM_START;        
        mbed::bd_addr_t applicationAddress = POST_APPLICATION_ADDR - MBED_ROM_START;

        update_client::BlockDeviceApplication activeApplication = update_client::BlockDeviceApplication(flashIAPBlockDevice, headerAddress, applicationAddress);
        update_client::UCErrorCode status = activeApplication.checkApplication();

        switch (status) {
        case update_client::UCErrorCode::UC_ERR_NONE: 
            tr_debug("SUCCESS: Application is valid.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_INVALID_HEADER:
            tr_debug("ERROR: Invalid Header.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_INVALID_CHECKSUM:
            tr_debug("ERROR: Invalid checksum - Application is corrupted\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_READ_FAILED:
            tr_debug("ERROR: Read application failed.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_HASH_INVALID:
            tr_debug("ERROR: Invalid hash - Application is corrupted\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_FIRMWARE_EMPTY:
            tr_debug("ERROR: The application is empty.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_PROGRAM_FAILED:
            tr_debug("ERROR: Program failed.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_CANNOT_INIT:
            tr_debug("ERROR: Cannot init.\r\n");
            break;
        case update_client::UCErrorCode::UC_ERR_CANNOT_START_THREAD:
            tr_debug("ERROR: Cannot start thread.\r\n");
            break;
        default:
            tr_debug("ERROR: Unknown error.\r\n");
            break;
        }

        update_client::CandidateApplications candidate_applications = update_client::CandidateApplications(
            flashIAPBlockDevice, 
            MBED_CONF_UPDATE_CLIENT_STORAGE_ADDRESS, 
            MBED_CONF_UPDATE_CLIENT_STORAGE_SIZE, 
            0x80, 
            MBED_CONF_UPDATE_CLIENT_STORAGE_LOCATIONS);


        // If the main app is not valid, install a valid candidate in memory if any
        if(status != update_client::UCErrorCode::UC_ERR_NONE){

            bool has_find_valid_candidate = false;

            for(int i; i < candidate_applications.getNbrOfSlots(); i++){
                auto app_candidate = candidate_applications.getBlockDeviceApplication(i);
                if(app_candidate.isValid()){
                    candidate_applications.installApplication(i, headerAddress);
                    has_find_valid_candidate = true;
                    break;
                }
            }

            if(has_find_valid_candidate == false){
                tr_error("No valid candidate in memory");
            }

        } else {
            // If the main app is valid, check for update in candidates. 
            tr_debug("Looking for update candidate");
            uint32_t newestSlotIndex;
            if(candidate_applications.hasValidNewerApplication(activeApplication, newestSlotIndex)){
                // If an update is available install it
                tr_debug("Newest app at index %d", newestSlotIndex);
                update_client::UCErrorCode status_update = candidate_applications.installApplication(newestSlotIndex, headerAddress);
                if(status_update != update_client::UCErrorCode::UC_ERR_NONE){
                    tr_error("Update failed");
                }
            } else {
                // start with main app
                tr_debug("No update found in candidates");
            }
        }

        // at this stage we directly branch to the main application
        void *sp = *((void **) POST_APPLICATION_ADDR + 0);  // NOLINT(readability/casting)
        void *pc = *((void **) POST_APPLICATION_ADDR + 1);  // NOLINT(readability/casting)
        tr_debug("Starting application at address 0x%08x (sp 0x%08x, pc 0x%08x)\r\n", POST_APPLICATION_ADDR, (uint32_t) sp, (uint32_t) pc);
        mbed_start_application(POST_APPLICATION_ADDR);

    } else {
        tr_debug("ERROR: flashIAPBlockDevice init failed.\r\n");
    }

    return 0;
}