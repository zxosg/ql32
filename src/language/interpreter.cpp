#include "ql32/interpreter.hpp"

#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ql32 {
namespace {

enum class Kind {
    End, Newline, Number, String, Identifier, Print,
    Plus, Minus, Star, Slash, Equal, LeftParen, RightParen, Colon
};

struct Token {
    Kind kind;
    std::string text;
    double number = 0;
    int line = 1;
    int column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else if (c == '\n') {
                tokens.push_back(single(Kind::Newline));
                advance();
                ++line_;
                column_ = 1;
            } else if (c == '\'') {
                skipComment();
            } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                       (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
                tokens.push_back(number());
            } else if (c == '"') {
                tokens.push_back(string());
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(identifier());
            } else {
                switch (c) {
                case '+': tokens.push_back(single(Kind::Plus)); break;
                case '-': tokens.push_back(single(Kind::Minus)); break;
                case '*': tokens.push_back(single(Kind::Star)); break;
                case '/': tokens.push_back(single(Kind::Slash)); break;
                case '=': tokens.push_back(single(Kind::Equal)); break;
                case '(': tokens.push_back(single(Kind::LeftParen)); break;
                case ')': tokens.push_back(single(Kind::RightParen)); break;
                case ':': tokens.push_back(single(Kind::Colon)); break;
                default: fail("unexpected character");
                }
                advance();
            }
        }
        tokens.push_back({Kind::End, "", 0, line_, column_});
        return tokens;
    }

private:
    std::string_view source_;
    std::size_t offset_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool atEnd() const { return offset_ >= source_.size(); }
    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }
    void advance() { ++offset_; ++column_; }
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error("line " + std::to_string(line_) + ", column " +
                                 std::to_string(column_) + ": " + message);
    }
    Token single(Kind kind) const { return {kind, std::string(1, peek()), 0, line_, column_}; }
    void skipComment() {
        while (!atEnd() && peek() != '\n') advance();
    }
    Token number() {
        const auto start = offset_;
        const int column = column_;
        bool dot = false;
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
            if (peek() == '.') {
                if (dot) fail("malformed number");
                dot = true;
            }
            advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("malformed exponent");
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        const std::string text(source_.substr(start, offset_ - start));
        double value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            fail("invalid number");
        return {Kind::Number, text, value, line_, column};
    }
    Token string() {
        const int column = column_;
        advance();
        std::string value;
        while (!atEnd()) {
            if (peek() == '"') {
                advance();
                if (peek() == '"') {
                    value.push_back('"');
                    advance();
                } else {
                    return {Kind::String, value, 0, line_, column};
                }
            } else if (peek() == '\n') {
                fail("newline in string literal");
            } else {
                value.push_back(peek());
                advance();
            }
        }
        fail("unterminated string literal");
    }
    Token identifier() {
        const auto start = offset_;
        const int column = column_;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        if (peek() == '%' || peek() == '$') advance();
        std::string text(source_.substr(start, offset_ - start));
        std::string upper = text;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "PRINT") return {Kind::Print, upper, 0, line_, column};
        if (upper.size() >= 3 && upper.size() <= 6 &&
            std::string_view("REMARK").starts_with(upper)) {
            skipComment();
            return {Kind::Newline, "", 0, line_, column};
        }
        return {Kind::Identifier, upper, 0, line_, column};
    }
};

enum class Op { Push, Load, Store, Add, Subtract, Multiply, Divide, Negate, Print };

struct Instruction {
    Op op;
    Value value = 0.0;
    std::string name;
    int line = 1;
    int column = 1;
};

class Compiler {
public:
    explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<Instruction> compile() {
        while (!check(Kind::End)) {
            if (match(Kind::Newline) || match(Kind::Colon)) continue;
            if (check(Kind::Number) && isLineNumber()) advance();
            statement();
            if (!check(Kind::End) && !check(Kind::Newline) && !check(Kind::Colon))
                error(peek(), "expected end of statement");
        }
        return std::move(code_);
    }

private:
    std::vector<Token> tokens_;
    std::vector<Instruction> code_;
    std::size_t current_ = 0;

    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool check(Kind kind) const { return peek().kind == kind; }
    bool match(Kind kind) {
        if (!check(kind)) return false;
        advance();
        return true;
    }
    const Token& advance() { if (!check(Kind::End)) ++current_; return previous(); }
    bool isLineNumber() const {
        return current_ + 1 < tokens_.size() &&
               (tokens_[current_ + 1].kind == Kind::Print ||
                (tokens_[current_ + 1].kind == Kind::Identifier &&
                 current_ + 2 < tokens_.size() && tokens_[current_ + 2].kind == Kind::Equal));
    }
    [[noreturn]] void error(const Token& token, const char* message) const {
        throw std::runtime_error("line " + std::to_string(token.line) + ", column " +
                                 std::to_string(token.column) + ": " + message);
    }
    void emit(Op op, const Token& token, Value value = 0.0, std::string name = {}) {
        code_.push_back({op, std::move(value), std::move(name), token.line, token.column});
    }
    void statement() {
        if (match(Kind::Print)) {
            const Token keyword = previous();
            if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
                expression();
            else
                emit(Op::Push, keyword, std::string{});
            emit(Op::Print, keyword);
            return;
        }
        if (check(Kind::Identifier) && current_ + 1 < tokens_.size() &&
            tokens_[current_ + 1].kind == Kind::Equal) {
            const Token name = advance();
            advance();
            expression();
            emit(Op::Store, name, 0.0, name.text);
            return;
        }
        error(peek(), "expected assignment or PRINT");
    }
    void expression() { term(); while (match(Kind::Plus) || match(Kind::Minus)) {
        const Token op = previous(); term(); emit(op.kind == Kind::Plus ? Op::Add : Op::Subtract, op);
    }}
    void term() { unary(); while (match(Kind::Star) || match(Kind::Slash)) {
        const Token op = previous(); unary(); emit(op.kind == Kind::Star ? Op::Multiply : Op::Divide, op);
    }}
    void unary() {
        if (match(Kind::Minus)) { const Token op = previous(); unary(); emit(Op::Negate, op); }
        else if (match(Kind::Plus)) unary();
        else primary();
    }
    void primary() {
        if (match(Kind::Number)) { const Token t = previous(); emit(Op::Push, t, t.number); return; }
        if (match(Kind::String)) { const Token t = previous(); emit(Op::Push, t, t.text); return; }
        if (match(Kind::Identifier)) { const Token t = previous(); emit(Op::Load, t, 0.0, t.text); return; }
        if (match(Kind::LeftParen)) {
            expression();
            if (!match(Kind::RightParen)) error(peek(), "expected ')'");
            return;
        }
        error(peek(), "expected expression");
    }
};

double numeric(const Value& value, const Instruction& at) {
    if (const auto* number = std::get_if<double>(&value)) return *number;
    throw std::runtime_error("line " + std::to_string(at.line) + ", column " +
                             std::to_string(at.column) + ": numeric value required");
}

void printValue(const Value& value, std::ostream& output) {
    if (const auto* number = std::get_if<double>(&value)) {
        output << *number;
    } else {
        output << std::get<std::string>(value);
    }
}

} // namespace

void Interpreter::execute(const std::string& source, std::ostream& output) {
    Compiler compiler(Lexer(source).scan());
    const auto code = compiler.compile();
    std::vector<Value> stack;
    auto pop = [&]() {
        if (stack.empty()) throw std::runtime_error("internal VM stack underflow");
        Value value = std::move(stack.back());
        stack.pop_back();
        return value;
    };

    for (const auto& instruction : code) {
        switch (instruction.op) {
        case Op::Push: stack.push_back(instruction.value); break;
        case Op::Load: {
            const auto found = variables_.find(instruction.name);
            if (found != variables_.end()) {
                stack.push_back(found->second);
            } else if (!instruction.name.empty() && instruction.name.back() == '$') {
                stack.emplace_back(std::string{});
            } else {
                stack.emplace_back(0.0);
            }
            break;
        }
        case Op::Store: {
            Value value = pop();
            if (!instruction.name.empty() && instruction.name.back() == '$') {
                if (!std::holds_alternative<std::string>(value))
                    throw std::runtime_error("line " + std::to_string(instruction.line) +
                                             ", column " + std::to_string(instruction.column) +
                                             ": string value required");
            } else {
                const double number = numeric(value, instruction);
                if (!instruction.name.empty() && instruction.name.back() == '%') {
                    if (std::trunc(number) != number ||
                        number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                        number > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                        throw std::runtime_error("line " + std::to_string(instruction.line) +
                                                 ", column " + std::to_string(instruction.column) +
                                                 ": Int32 value out of range");
                }
            }
            variables_[instruction.name] = std::move(value);
            break;
        }
        case Op::Negate: {
            const double value = -numeric(pop(), instruction);
            if (!std::isfinite(value)) throw std::runtime_error("numeric overflow");
            stack.push_back(value);
            break;
        }
        case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide: {
            Value right = pop();
            Value left = pop();
            if (instruction.op == Op::Add &&
                std::holds_alternative<std::string>(left) &&
                std::holds_alternative<std::string>(right)) {
                stack.push_back(std::get<std::string>(left) + std::get<std::string>(right));
                break;
            }
            const double a = numeric(left, instruction);
            const double b = numeric(right, instruction);
            if (instruction.op == Op::Divide && b == 0)
                throw std::runtime_error("line " + std::to_string(instruction.line) +
                                         ", column " + std::to_string(instruction.column) +
                                         ": division by zero");
            double result = 0;
            if (instruction.op == Op::Add) result = a + b;
            else if (instruction.op == Op::Subtract) result = a - b;
            else if (instruction.op == Op::Multiply) result = a * b;
            else result = a / b;
            if (!std::isfinite(result)) throw std::runtime_error("numeric overflow");
            stack.push_back(result);
            break;
        }
        case Op::Print:
            printValue(pop(), output);
            output << '\n';
            break;
        }
    }
}

void Interpreter::clear() { variables_.clear(); }

} // namespace ql32
