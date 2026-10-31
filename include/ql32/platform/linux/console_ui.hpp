#pragma once

#include "ql32/ui/service.hpp"

#include <istream>
#include <memory>
#include <ostream>

namespace ql32::platform::linux_host {

class ConsoleUi final {
public:
    ConsoleUi(std::shared_ptr<ui::QueuedService> service,
              std::istream& input, std::ostream& output);

    // Presents and resolves the next queued request. Returns false when empty.
    bool dispatchNext();

private:
    std::shared_ptr<ui::QueuedService> service_;
    std::istream& input_;
    std::ostream& output_;
};

} // namespace ql32::platform::linux_host
