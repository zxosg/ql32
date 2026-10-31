#pragma once

#include "ql32/ui/service.hpp"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace ql32::platform::esp32 {

// Non-blocking line-oriented UI presenter. The caller feeds completed input
// lines and invokes resume only after consumeLine reports the request done.
class LineUiPresenter final {
public:
    LineUiPresenter(std::shared_ptr<ui::QueuedService> service, std::ostream& output);

    bool beginNext();
    bool active() const noexcept { return request_.has_value(); }
    bool consumeLine(const std::string& line);

private:
    enum class DialogStage { Field, Submit };

    std::shared_ptr<ui::QueuedService> service_;
    std::ostream& output_;
    std::optional<ui::Request> request_;
    std::size_t fieldIndex_ = 0;
    std::vector<ui::FieldValue> fieldValues_;
    DialogStage dialogStage_ = DialogStage::Field;

    void present();
    void presentNextDialogField();
    void finish();
    bool consumeMenuLine(const std::string& line);
    bool consumeDialogLine(const std::string& line);
};

} // namespace ql32::platform::esp32
