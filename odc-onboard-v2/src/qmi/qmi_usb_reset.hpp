#pragma once
#include <string>

namespace odc {
namespace qmi {

/**
 * Resolve the USB device for a cdc-wdm control node and cycle it.
 * Tries authorized 0→1, then falls back to writing "1" to the USB device reset file.
 * @return true if at least one reset mechanism was written successfully.
 */
bool usb_reset_device(const std::string& cdc_wdm_path);

} // namespace qmi
} // namespace odc
