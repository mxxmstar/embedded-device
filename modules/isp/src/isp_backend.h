#pragma once
#include "isp/isp_controller.h"

namespace isp::detail {
class IspBackend {
public:
    virtual ~IspBackend() = default;
    virtual bool Prepare(const IspOptions&, IspTopology&, std::string&) = 0;
    virtual bool Start(std::string&) = 0;
    virtual bool Stop(std::string&) = 0;
    virtual void Reset() noexcept = 0;
};
std::unique_ptr<IspBackend> CreateIspBackend();
} // namespace isp::detail
