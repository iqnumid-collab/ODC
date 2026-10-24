#pragma once
#include "qmi_services.h"
#include <string>
#include <functional>

namespace uqmi {

struct ModemHwInfo {
    std::string manufacturer;   // e.g. "Quectel"
    std::string model;          // e.g. "EC25"
    std::string revision;       // firmware version
    std::string hw_revision;    // hardware revision
    std::string imei;
    std::string imsi;
    std::string iccid;
    std::string esn;
    std::string meid;

    void log_summary() const;
    bool valid() const { return !model.empty(); }
};

using HwInfoCb = std::function<void(const ModemHwInfo&)>;

class HwInfoCollector {
public:
    void collect(QmiServices& qmi, HwInfoCb done);

private:
    QmiServices* qmi_ = nullptr;
    HwInfoCb     done_;
    ModemHwInfo  info_;

    void step_manufacturer();
    void step_model();
    void step_revision();
    void step_hw_revision();
    void step_ids();
    void step_imsi();
    void step_iccid();
    void finish();
};

} // namespace uqmi
