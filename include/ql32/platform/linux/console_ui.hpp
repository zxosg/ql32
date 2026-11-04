#pragma once

#include "ql32/ui/service.hpp"
#include "ql32/ui/graphic_presenter.hpp"

#include <istream>
#include <memory>
#include <ostream>

namespace ql32::platform::linux_host {

class ConsoleUi final {
public:
    ConsoleUi(std::shared_ptr<ui::QueuedService> service,
              std::istream& input, std::ostream& output,
              std::shared_ptr<graphics::WindowedService> graphics = {});

    // Presents and resolves the next queued request. Returns false when empty.
    bool dispatchNext();

private:
    std::shared_ptr<ui::QueuedService> service_;
    std::istream& input_;
    std::ostream& output_;
    std::unique_ptr<ui::GraphicPresenter> graphic_;
};

} // namespace ql32::platform::linux_host
