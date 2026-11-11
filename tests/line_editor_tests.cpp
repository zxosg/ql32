#include "ql32/ui/line_editor.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ql32::input::KeyEvent key(ql32::input::Key value, char32_t character = 0,
                          bool pressed = true) {
    ql32::input::KeyEvent event;
    event.key = value;
    event.character = character;
    event.pressed = pressed;
    return event;
}

void testInsertionAndEditing() {
    ql32::ui::LineEditor editor;
    require(editor.handleKey(key(ql32::input::Key::Character, 'a')) ==
                ql32::ui::LineEditorAction::Changed,
            "character insertion did not change the line");
    editor.handleKey(key(ql32::input::Key::Character, 'b'));
    editor.handleKey(key(ql32::input::Key::Character, 'c'));
    editor.handleKey(key(ql32::input::Key::Left));
    editor.handleKey(key(ql32::input::Key::Character, 'X'));
    require(editor.text() == "abXc" && editor.cursor() == 3,
            "insertion at the cursor produced the wrong line");
    editor.handleKey(key(ql32::input::Key::Backspace));
    require(editor.text() == "abc" && editor.cursor() == 2,
            "backspace did not remove the character before the cursor");
    editor.handleKey(key(ql32::input::Key::Home));
    editor.handleKey(key(ql32::input::Key::Delete));
    require(editor.text() == "bc" && editor.cursor() == 0,
            "delete at the cursor produced the wrong line");
    editor.handleKey(key(ql32::input::Key::End));
    require(editor.cursor() == editor.text().size(), "End did not move to line end");
}

void testRecallAndSubmission() {
    ql32::ui::LineEditor editor;
    editor.setText("PRINT 1");
    editor.rememberCurrentLine();
    editor.clear();
    require(editor.handleKey(key(ql32::input::Key::Up)) ==
                ql32::ui::LineEditorAction::Changed &&
                editor.text() == "PRINT 1" && editor.cursor() == 7,
            "Up on an empty line did not recall the previous command");
    require(editor.handleKey(key(ql32::input::Key::Up)) ==
                ql32::ui::LineEditorAction::Ignored,
            "Up replaced a non-empty line");
    require(editor.handleKey(key(ql32::input::Key::Enter)) ==
                ql32::ui::LineEditorAction::Submit && editor.text() == "PRINT 1",
            "Enter should request submission without discarding the line");
}

void testLimitsAndRawProtocolText() {
    ql32::ui::LineEditor editor;
    require(editor.handleKey(key(ql32::input::Key::Character, 'x', false)) ==
                ql32::ui::LineEditorAction::Ignored && editor.text().empty(),
            "key release changed the input line");
    require(!editor.insertRawCharacter('\t'),
            "ordinary text input accepted a tab character");
    require(editor.insertRawCharacter('\t', true) && editor.text() == "\t",
            "protocol input could not preserve a tab separator");

    editor.setText(std::string(ql32::ui::LineEditor::maximumLength + 4, 'z'));
    require(editor.text().size() == ql32::ui::LineEditor::maximumLength,
            "setText exceeded the line length limit");
    require(editor.handleKey(key(ql32::input::Key::Character, 'x')) ==
                ql32::ui::LineEditorAction::Ignored &&
                editor.text().size() == ql32::ui::LineEditor::maximumLength,
            "insertion exceeded the line length limit");
}

} // namespace

int main() {
    try {
        testInsertionAndEditing();
        testRecallAndSubmission();
        testLimitsAndRawProtocolText();
        std::cout << "Line editor tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "line editor test failed: " << error.what() << '\n';
        return 1;
    }
}
