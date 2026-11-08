#pragma once

#include "ql32/graphics/windowed_service.hpp"
#include "ql32/input/service.hpp"
#include "ql32/ui/service.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ql32::ui {

// One foreground request, with a display-independent layout and input state.
// Input requests remain line-oriented; all other request kinds have a panel.
class GraphicPresenter {
public:
    GraphicPresenter(std::shared_ptr<QueuedService> service,
                     std::shared_ptr<graphics::WindowedService> graphics);
    ~GraphicPresenter();
    bool beginNext();
    bool active() const noexcept { return request_.has_value(); }
    bool graphicActive() const noexcept;
    bool inputActive() const noexcept;
    void refresh();
    void screenChanged();
    bool handleKey(const input::KeyEvent& event);
    void handleSerialByte(char byte, std::uint32_t nowMs);
    void flushSerialEscape(std::uint32_t nowMs);
    bool consumeLine(const std::string& line);
    void cancelActive();
    void composeRow(std::uint32_t y, std::uint32_t x,
                    std::vector<graphics::ColorIndex>& row) const;
    graphics::Rect bounds() const noexcept { return bounds_; }

private:
    struct Row {
        std::string text;
        int field = -1;
        bool enabled = true;
    };
    std::shared_ptr<QueuedService> service_;
    std::shared_ptr<graphics::WindowedService> graphics_;
    std::optional<Request> request_;
    graphics::Rect bounds_{};
    std::unique_ptr<graphics::IndexedCanvas> panel_;
    std::optional<InfoToast> infoToast_;
    std::unique_ptr<graphics::IndexedCanvas> infoPanel_;
    graphics::Rect infoBounds_{};
    std::chrono::steady_clock::time_point infoExpiresAt_{};
    bool infoFlashPhase_ = false;
    std::vector<Row> rows_;
    std::vector<std::string> edits_;
    std::vector<FieldValue> values_;
    std::string inputEdit_;
    std::size_t focus_ = 0;
    std::size_t scroll_ = 0;
    bool confirmYes_ = false;
    std::string error_;
    int serialEscapeState_ = 0;
    std::uint32_t serialEscapeAt_ = 0;
    std::uint32_t screenWidth_ = 0;
    std::uint32_t screenHeight_ = 0;
    int buttonColumnWidth_ = 0;

    void finish();
    void layout();
    void refreshInfo();
    void layoutInfo();
    void paint();
    int titleHeight() const noexcept;
    int footerHeight() const noexcept;
    int rowPitch() const noexcept;
    int rightButtonWidth() const noexcept;
    void rebuildRows();
    void ensureFocusVisible();
    void moveFocus(int delta);
    bool submit();
    void changeChoice(int delta);
    bool editCharacter(char character);
    bool backspace();
};

} // namespace ql32::ui
