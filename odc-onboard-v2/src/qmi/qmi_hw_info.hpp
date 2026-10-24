#pragma once
#include "qmi_services.hpp"
#include <string>
#include <functional>

namespace odc {
namespace qmi {

struct ModemHwInfo {
    std::string manufacturer;
    std::string model;
    std::string revision;
    std::string hw_revision;
    std::string imei;
    std::string imsi;
    std::string iccid;
    std::string esn;
    std::string meid;
};

using HwInfoCb = std::function<void(const ModemHwInfo&)>;

class QmiHwInfo {
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

} // namespace qmi
} // namespace odc
