#include "isp_backend.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using isp::IspController;
namespace {
std::vector<std::string> calls;
bool fail_prepare = false, fail_start = false, fail_stop = false;
void Check(bool value, const char* expression) {
    if (!value) { std::cerr << "Failed: " << expression << '\n'; std::exit(1); }
}
#define CHECK(x) Check((x), #x)
}
namespace isp::detail {
class FakeBackend final : public IspBackend {
public:
    bool Prepare(const IspOptions&, IspTopology& topology, std::string& error) override {
        calls.push_back("prepare"); topology.sensor_entity = "fake-sensor";
        if (fail_prepare) error = "prepare failure";
        return !fail_prepare;
    }
    bool Start(std::string& error) override {
        calls.push_back("start"); if (fail_start) error = "start failure";
        return !fail_start;
    }
    bool Stop(std::string& error) override {
        calls.push_back("stop"); if (fail_stop) error = "stop failure";
        return !fail_stop;
    }
    void Reset() noexcept override { calls.push_back("reset"); }
};
std::unique_ptr<IspBackend> CreateIspBackend() { return std::make_unique<FakeBackend>(); }
}

int main() {
    std::string error;
    {
        IspController controller;
        CHECK(!controller.Start(error)); CHECK(!error.empty()); CHECK(calls.empty());
        CHECK(controller.Stop(error)); CHECK(error.empty());
        fail_prepare = true;
        CHECK(!controller.Prepare({}, error)); CHECK(error == "prepare failure");
        CHECK(controller.GetState() == IspController::State::Empty);
        CHECK(controller.Topology().sensor_entity.empty());
        CHECK(calls == (std::vector<std::string>{"prepare", "reset"}));
        fail_prepare = false; calls.clear();
        CHECK(controller.Prepare({}, error)); CHECK(error.empty());
        CHECK(controller.Topology().sensor_entity == "fake-sensor");
        CHECK(!controller.Prepare({}, error)); CHECK(calls.size() == 1);
        CHECK(controller.Start(error)); CHECK(controller.Start(error)); CHECK(calls.size() == 2);
        fail_stop = true;
        CHECK(!controller.Stop(error)); CHECK(error == "stop failure");
        CHECK(controller.GetState() == IspController::State::Running);
        fail_stop = false;
        CHECK(controller.Stop(error)); CHECK(controller.Stop(error));
        CHECK(controller.GetState() == IspController::State::Prepared);
        CHECK(controller.Start(error));
        calls.clear();
    }
    CHECK(calls == (std::vector<std::string>{"reset"})); // destructor releases running backend
    calls.clear();
    {
        IspController controller;
        CHECK(controller.Prepare({}, error)); fail_start = true;
        CHECK(!controller.Start(error)); CHECK(error == "start failure");
        CHECK(controller.GetState() == IspController::State::Empty);
        CHECK(controller.Topology().sensor_entity.empty());
        CHECK(calls == (std::vector<std::string>{"prepare", "start", "reset"}));
        fail_start = false;
        CHECK(controller.Prepare({}, error)); CHECK(controller.Start(error));
        controller.Reset(); calls.clear(); controller.Reset(); CHECK(calls.empty());
    }
    std::cout << "ISP lifecycle: success, repeated calls, failure cleanup, retry and destruction passed.\n";
}
