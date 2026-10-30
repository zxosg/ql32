#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <variant>

namespace ql32 {

using Value = std::variant<double, std::string>;

class Interpreter {
public:
    void execute(const std::string& source, std::ostream& output);
    void clear();

private:
    std::unordered_map<std::string, Value> variables_;
};

} // namespace ql32
