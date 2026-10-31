#pragma once

#include <iosfwd>
#include <cstddef>
#include <map>
#include <string>
#include <stop_token>
#include <unordered_map>
#include <variant>

namespace ql32 {

using Value = std::variant<double, std::string>;

class Interpreter {
public:
    void execute(const std::string& source, std::ostream& output, std::stop_token stop = {});
    void executeInteractive(const std::string& source, std::ostream& output);
    void runRepl(std::istream& input, std::ostream& output, std::ostream& diagnostics,
                 bool showPrompt = false);
    void clear();

private:
    std::unordered_map<std::string, Value> variables_;
    std::size_t output_column_ = 0;
    std::map<int, std::string> program_;
};

} // namespace ql32
