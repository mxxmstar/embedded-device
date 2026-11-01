#include "isp_backend.h"

namespace isp {
IspController::IspController() = default;
IspController::~IspController() { Reset(); }

bool IspController::Prepare(const IspOptions& options, std::string& error) {
    error.clear();
    if (state_ != State::Empty) {
        error = "ISP is already prepared; Reset before preparing another camera";
        return false;
    }
    auto backend = detail::CreateIspBackend();
    IspTopology topology;
    if (!backend->Prepare(options, topology, error)) {
        backend->Reset();
        return false;
    }
    backend_ = std::move(backend);
    topology_ = std::move(topology);
    state_ = State::Prepared;
    return true;
}

bool IspController::Start(std::string& error) {
    error.clear();
    if (state_ == State::Running) return true;
    if (state_ != State::Prepared) {
        error = "Prepare ISP before Start";
        return false;
    }
    if (!backend_->Start(error)) {
        // 只要 rkisp_cl_start 失败，就必须重置 ISP 后端，防止残留
        Reset();
        return false;
    }
    state_ = State::Running;
    return true;
}

bool IspController::Stop(std::string& error) {
    error.clear();
    if (state_ != State::Running) return true;
    if (!backend_->Stop(error)) return false;
    state_ = State::Prepared;
    return true;
}

void IspController::Reset() noexcept {
    if (backend_) backend_->Reset();
    backend_.reset();
    topology_ = {};
    state_ = State::Empty;
}
} // namespace isp
