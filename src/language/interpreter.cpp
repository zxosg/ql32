#include "ql32/interpreter.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ql32 {
namespace {

enum class Kind {
    End, Newline, Number, String, Identifier, Print, If, Then, Else, EndKeyword, Dim,
    Select, On, Remainder,
    For, To, Step, Next, Repeat, Exit, Define, Procedure, Function, Local, Return,
    Plus, Minus, Star, Slash, Div, Mod, Power,
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Not,
    Semicolon, Comma, Exclamation, Backslash,
    LeftParen, RightParen, Colon
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
                case '^': tokens.push_back(single(Kind::Power)); break;
                case '=': tokens.push_back(single(Kind::Equal)); break;
                case '<':
                    if (peek(1) == '=') { tokens.push_back({Kind::LessEqual, "<=", 0, line_, column_}); advance(); }
                    else if (peek(1) == '>') { tokens.push_back({Kind::NotEqual, "<>", 0, line_, column_}); advance(); }
                    else tokens.push_back(single(Kind::Less));
                    break;
                case '>':
                    if (peek(1) == '=') { tokens.push_back({Kind::GreaterEqual, ">=", 0, line_, column_}); advance(); }
                    else tokens.push_back(single(Kind::Greater));
                    break;
                case ';': tokens.push_back(single(Kind::Semicolon)); break;
                case ',': tokens.push_back(single(Kind::Comma)); break;
                case '!': tokens.push_back(single(Kind::Exclamation)); break;
                case '\\': tokens.push_back(single(Kind::Backslash)); break;
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
        if (upper == "DIM") return {Kind::Dim, upper, 0, line_, column};
        if (upper == "SELECT") return {Kind::Select, upper, 0, line_, column};
        if (upper == "ON") return {Kind::On, upper, 0, line_, column};
        if (upper == "REMAINDER") return {Kind::Remainder, upper, 0, line_, column};
        if (upper == "IF") return {Kind::If, upper, 0, line_, column};
        if (upper == "THEN") return {Kind::Then, upper, 0, line_, column};
        if (upper == "ELSE") return {Kind::Else, upper, 0, line_, column};
        if (upper == "END") return {Kind::EndKeyword, upper, 0, line_, column};
        if (upper == "FOR") return {Kind::For, upper, 0, line_, column};
        if (upper == "TO") return {Kind::To, upper, 0, line_, column};
        if (upper == "STEP") return {Kind::Step, upper, 0, line_, column};
        if (upper == "NEXT") return {Kind::Next, upper, 0, line_, column};
        if (upper == "REPEAT") return {Kind::Repeat, upper, 0, line_, column};
        if (upper == "EXIT") return {Kind::Exit, upper, 0, line_, column};
        if (upper == "DEFINE") return {Kind::Define, upper, 0, line_, column};
        if (upper == "PROCEDURE") return {Kind::Procedure, upper, 0, line_, column};
        if (upper == "FUNCTION") return {Kind::Function, upper, 0, line_, column};
        if (upper == "LOCAL") return {Kind::Local, upper, 0, line_, column};
        if (upper == "RETURN") return {Kind::Return, upper, 0, line_, column};
        if (upper == "DIV") return {Kind::Div, upper, 0, line_, column};
        if (upper == "MOD") return {Kind::Mod, upper, 0, line_, column};
        if (upper == "AND") return {Kind::And, upper, 0, line_, column};
        if (upper == "OR") return {Kind::Or, upper, 0, line_, column};
        if (upper == "XOR") return {Kind::Xor, upper, 0, line_, column};
        if (upper == "NOT") return {Kind::Not, upper, 0, line_, column};
        if (upper.size() >= 3 && upper.size() <= 6 &&
            std::string_view("REMARK").starts_with(upper)) {
            skipComment();
            return {Kind::Newline, "", 0, line_, column};
        }
        return {Kind::Identifier, upper, 0, line_, column};
    }
};

enum class PrintSeparator { None, Semicolon, Exclamation, Comma, Backslash };
enum class Op {
    Push, Load, Store, Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
    Power, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Negate, Not, Print, PrintSeparator, PrintEnd, JumpIfFalse, Jump,
    ForInit, ForNext, LoopNext, ExitLoop, Call, Return, FunctionEnd,
    DimArray, ArrayGet, ArraySet, SelectBegin, SelectMatch, SelectRemainder, SelectEnd
};

struct ForItemSpec {
    bool hasEnd = false;
    bool hasStep = false;
};

struct SelectItemSpec {
    bool hasEnd = false;
};

struct ForRange {
    double start = 0;
    double end = 0;
    double step = 1;
    bool single = false;
};

struct ForState {
    std::vector<ForRange> ranges;
    std::size_t item = 0;
};

struct RuntimeFrame {
    std::size_t returnAddress = 0;
    bool function = false;
    std::unordered_map<std::string, Value> locals;
};

struct RuntimeArray {
    std::vector<std::size_t> lengths;
    std::vector<Value> values;
};

struct SelectState {
    double value = 0;
    bool matched = false;
};

struct Instruction {
    Op op;
    Value value = 0.0;
    std::string name;
    int line = 1;
    int column = 1;
    PrintSeparator separator = PrintSeparator::None;
    std::size_t target = 0;
    std::size_t exitTarget = 0;
    std::size_t loopId = 0;
    std::vector<ForItemSpec> forItems;
    bool explicitNext = false;
    bool clearTarget = false;
    std::size_t argCount = 0;
    bool expectsValue = false;
    std::vector<std::string> parameters;
    std::vector<std::string> localNames;
    std::size_t rank = 0;
    std::vector<SelectItemSpec> selectItems;
};

struct DefinitionInfo {
    std::size_t address = 0;
    bool function = false;
    std::vector<std::string> parameters;
    std::vector<std::string> locals;
};

class Compiler {
public:
    explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<Instruction> compile() {
        block();
        if (!check(Kind::End)) error(peek(), "unexpected block terminator");
        resolveCalls();
        return std::move(code_);
    }

private:
    std::vector<Token> tokens_;
    std::vector<Instruction> code_;
    std::unordered_map<std::string, DefinitionInfo> definitions_;
    std::unordered_map<std::string, std::size_t> arrays_;
    std::size_t current_ = 0;
    struct LoopContext {
        std::size_t id;
        std::string name;
        std::size_t body;
        bool isFor;
        std::vector<std::size_t> nextInstructions;
        std::vector<std::size_t> exitInstructions;
    };
    std::vector<LoopContext> loops_;
    std::size_t nextLoopId_ = 1;
    bool inDefinition_ = false;

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
                tokens_[current_ + 1].kind == Kind::If ||
                tokens_[current_ + 1].kind == Kind::Else ||
                tokens_[current_ + 1].kind == Kind::EndKeyword ||
                tokens_[current_ + 1].kind == Kind::For ||
                tokens_[current_ + 1].kind == Kind::Next ||
                tokens_[current_ + 1].kind == Kind::Repeat ||
                tokens_[current_ + 1].kind == Kind::Exit ||
                tokens_[current_ + 1].kind == Kind::Define ||
                tokens_[current_ + 1].kind == Kind::Dim ||
                tokens_[current_ + 1].kind == Kind::Select ||
                tokens_[current_ + 1].kind == Kind::Equal ||
                tokens_[current_ + 1].kind == Kind::Local ||
                tokens_[current_ + 1].kind == Kind::Return ||
                tokens_[current_ + 1].kind == Kind::Newline ||
                tokens_[current_ + 1].kind == Kind::End ||
                (tokens_[current_ + 1].kind == Kind::Identifier &&
                 current_ + 2 < tokens_.size() && tokens_[current_ + 2].kind == Kind::Equal));
    }
    [[noreturn]] void error(const Token& token, const char* message) const {
        throw std::runtime_error("line " + std::to_string(token.line) + ", column " +
                                 std::to_string(token.column) + ": " + message);
    }
    [[noreturn]] void error(const Instruction& instruction, const char* message) const {
        throw std::runtime_error("line " + std::to_string(instruction.line) + ", column " +
                                 std::to_string(instruction.column) + ": " + message);
    }
    void emit(Op op, const Token& token, Value value = 0.0, std::string name = {},
              PrintSeparator separator = PrintSeparator::None) {
        code_.push_back({op, std::move(value), std::move(name), token.line,
                         token.column, separator});
    }
    void resolveCalls() {
        for (Instruction& instruction : code_) {
            if (instruction.op != Op::Call) continue;
            const auto found = definitions_.find(instruction.name);
            if (found == definitions_.end()) {
                const auto array = arrays_.find(instruction.name);
                if (instruction.expectsValue && array != arrays_.end()) {
                    if (instruction.argCount != array->second)
                        error(instruction, "incorrect array rank");
                    instruction.op = Op::ArrayGet;
                    instruction.rank = array->second;
                    continue;
                }
                error(instruction, "undefined procedure or function");
            }
            const DefinitionInfo& definition = found->second;
            if (instruction.expectsValue != definition.function)
                error(instruction, "procedure/function used in the wrong context");
            if (instruction.argCount != definition.parameters.size())
                error(instruction, "incorrect argument count");
            instruction.target = definition.address;
            instruction.parameters = definition.parameters;
            instruction.localNames = definition.locals;
        }
    }
    void block() {
        while (!check(Kind::End) && !check(Kind::Else) && !check(Kind::EndKeyword)) {
            if (match(Kind::Newline) || match(Kind::Colon)) continue;
            if (check(Kind::Number) && isLineNumber()) {
                advance();
                if (check(Kind::Newline) || check(Kind::Colon) || check(Kind::End) ||
                    check(Kind::Else) || check(Kind::EndKeyword)) continue;
            }
            statement();
            if (!check(Kind::End) && !check(Kind::Newline) && !check(Kind::Colon) &&
                !check(Kind::Else) && !check(Kind::EndKeyword))
                error(peek(), "expected end of statement");
        }
    }
    void requireLineBreak() {
        if (!match(Kind::Newline) && !match(Kind::Colon))
            error(peek(), "expected end of line after block IF");
        while (match(Kind::Newline) || match(Kind::Colon)) {}
    }
    void blockIf() {
        const Token keyword = advance();
        expression();
        if (!match(Kind::Then)) error(peek(), "expected THEN");
        const std::size_t branch = code_.size();
        emit(Op::JumpIfFalse, keyword);
        requireLineBreak();
        block();
        if (match(Kind::Else)) {
            const Token elseToken = previous();
            const std::size_t skipElse = code_.size();
            emit(Op::Jump, elseToken);
            code_[branch].target = code_.size();
            if (check(Kind::If)) {
                blockIf();
            } else {
                requireLineBreak();
                block();
            }
            code_[skipElse].target = code_.size();
        } else {
            code_[branch].target = code_.size();
        }
        while (match(Kind::Newline) || match(Kind::Colon)) {}
        if (!match(Kind::EndKeyword)) error(peek(), "expected END IF");
        if (!match(Kind::If)) error(peek(), "expected IF after END");
        if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
            error(peek(), "expected end of statement");
    }
    LoopContext& findLoop(const Token& nameToken) {
        for (auto loop = loops_.rbegin(); loop != loops_.rend(); ++loop)
            if (loop->name == nameToken.text) return *loop;
        error(nameToken, "loop name does not identify an open loop");
    }
    void emitForNext(const Token& keyword, const Token& nameToken, bool explicitNext = false) {
        LoopContext& loop = findLoop(nameToken);
        if (!loop.isFor) {
            Instruction instruction{Op::LoopNext, 0.0, nameToken.text, keyword.line, keyword.column};
            instruction.target = loop.body;
            instruction.loopId = loop.id;
            code_.push_back(std::move(instruction));
            return;
        }
        Instruction instruction{Op::ForNext, 0.0, nameToken.text, keyword.line, keyword.column};
        instruction.target = loop.body;
        instruction.loopId = loop.id;
        instruction.explicitNext = explicitNext;
        code_.push_back(std::move(instruction));
        loop.nextInstructions.push_back(code_.size() - 1);
    }
    void emitExit(const Token& keyword, const Token& nameToken) {
        LoopContext& loop = findLoop(nameToken);
        Instruction instruction{Op::ExitLoop, 0.0, nameToken.text, keyword.line, keyword.column};
        instruction.loopId = loop.id;
        code_.push_back(std::move(instruction));
        loop.exitInstructions.push_back(code_.size() - 1);
    }
    std::size_t callArguments() {
        if (!match(Kind::LeftParen)) error(peek(), "expected '(' before arguments");
        std::size_t count = 0;
        if (!check(Kind::RightParen)) {
            do {
                expression();
                ++count;
            } while (match(Kind::Comma));
        }
        if (!match(Kind::RightParen)) error(peek(), "expected ')' after arguments");
        return count;
    }
    void emitCall(const Token& name, std::size_t arguments, bool expectsValue) {
        Instruction instruction{Op::Call, 0.0, name.text, name.line, name.column};
        instruction.argCount = arguments;
        instruction.expectsValue = expectsValue;
        code_.push_back(std::move(instruction));
    }
    void blockDim() {
        const Token keyword = advance();
        if (!check(Kind::Identifier)) error(peek(), "expected array name after DIM");
        const Token name = advance();
        if (arrays_.contains(name.text)) error(name, "array is already dimensioned");
        const std::size_t rank = callArguments();
        if (rank == 0) error(name, "array needs at least one dimension");
        arrays_[name.text] = rank;
        Instruction instruction{Op::DimArray, 0.0, name.text, keyword.line, keyword.column};
        instruction.rank = rank;
        code_.push_back(std::move(instruction));
    }
    void selectArm() {
        while (!check(Kind::End) && !check(Kind::EndKeyword) && !check(Kind::Equal)) {
            if (match(Kind::Newline) || match(Kind::Colon)) continue;
            if (check(Kind::Number) && isLineNumber()) {
                advance();
                if (check(Kind::Newline) || check(Kind::Colon) || check(Kind::End)) continue;
            }
            statement();
            if (!check(Kind::End) && !check(Kind::Newline) && !check(Kind::Colon) &&
                !check(Kind::EndKeyword) && !check(Kind::Equal))
                error(peek(), "expected end of SELECT arm statement");
        }
    }
    void blockSelect() {
        const Token keyword = advance();
        if (!match(Kind::On)) error(peek(), "expected ON after SELECT");
        expression();
        Instruction begin{Op::SelectBegin, 0.0, {}, keyword.line, keyword.column};
        code_.push_back(std::move(begin));
        if (!match(Kind::Newline)) error(peek(), "expected end of line after SELECT ON");
        while (match(Kind::Newline)) {}

        std::vector<std::size_t> endJumps;
        bool hasRemainder = false;
        std::size_t armCount = 0;
        while (check(Kind::Equal) ||
               (check(Kind::Number) && isLineNumber() && current_ + 1 < tokens_.size() &&
                tokens_[current_ + 1].kind == Kind::Equal)) {
            if (hasRemainder) error(peek(), "REMAINDER must be the final SELECT arm");
            if (check(Kind::Number) && isLineNumber()) advance();
            const Token armToken = peek();
            if (!match(Kind::Equal)) error(peek(), "expected '=' at SELECT arm");
            ++armCount;
            std::size_t falseJump = code_.size();
            if (match(Kind::Remainder)) {
                hasRemainder = true;
                Instruction remainder{Op::SelectRemainder, 0.0, {}, armToken.line, armToken.column};
                code_.push_back(std::move(remainder));
                falseJump = code_.size();
            } else {
                std::vector<SelectItemSpec> items;
                while (true) {
                    expression();
                    SelectItemSpec item;
                    if (match(Kind::To)) {
                        item.hasEnd = true;
                        expression();
                    }
                    items.push_back(item);
                    if (!match(Kind::Comma)) break;
                }
                Instruction compare{Op::SelectMatch, 0.0, {}, armToken.line, armToken.column};
                compare.selectItems = std::move(items);
                code_.push_back(std::move(compare));
                falseJump = code_.size();
                emit(Op::JumpIfFalse, armToken);
            }
            selectArm();
            Instruction skip{Op::Jump, 0.0, {}, armToken.line, armToken.column};
            code_.push_back(std::move(skip));
            endJumps.push_back(code_.size() - 1);
            if (falseJump != code_.size() - 1 && !hasRemainder)
                code_[falseJump].target = code_.size();
            while (match(Kind::Newline) || match(Kind::Colon)) {}
        }
        if (armCount == 0) error(peek(), "SELECT requires at least one arm");
        if (!match(Kind::EndKeyword)) error(peek(), "expected END SELECT");
        if (!match(Kind::Select)) error(peek(), "expected SELECT after END");
        Instruction finish{Op::SelectEnd, 0.0, {}, keyword.line, keyword.column};
        code_.push_back(std::move(finish));
        for (const std::size_t jump : endJumps) code_[jump].target = code_.size();
        if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
            error(peek(), "expected end of statement");
    }
    void blockDefinition() {
        if (inDefinition_) error(peek(), "nested definitions are not supported");
        const Token defineKeyword = advance();
        bool isFunction = false;
        if (match(Kind::Procedure)) {
            isFunction = false;
        } else if (match(Kind::Function)) {
            isFunction = true;
        } else {
            error(peek(), "expected PROCEDURE or FUNCTION after DEFINE");
        }
        if (!check(Kind::Identifier)) error(peek(), "expected definition name");
        const Token name = advance();
        std::vector<std::string> parameters;
        if (match(Kind::LeftParen)) {
            if (!check(Kind::RightParen)) {
                do {
                    if (!check(Kind::Identifier)) error(peek(), "expected parameter name");
                    const Token parameter = advance();
                    if (std::find(parameters.begin(), parameters.end(), parameter.text) != parameters.end())
                        error(parameter, "duplicate parameter name");
                    parameters.push_back(parameter.text);
                } while (match(Kind::Comma));
            }
            if (!match(Kind::RightParen)) error(peek(), "expected ')' after parameters");
        }

        if (definitions_.contains(name.text)) error(name, "duplicate definition name");
        const std::size_t skipIndex = code_.size();
        emit(Op::Jump, defineKeyword);
        DefinitionInfo definition;
        definition.address = code_.size();
        definition.function = isFunction;
        definition.parameters = parameters;
        definitions_.emplace(name.text, definition);

        if (!match(Kind::Newline)) error(peek(), "expected end of line after definition header");
        while (match(Kind::Newline)) {}
        std::vector<std::string> locals;
        while (true) {
            if (check(Kind::Number) && isLineNumber()) advance();
            while (match(Kind::Newline)) {}
            if (!match(Kind::Local)) break;
            if (!check(Kind::Identifier)) error(peek(), "expected local variable name");
            do {
                if (!check(Kind::Identifier)) error(peek(), "expected local variable name");
                const Token local = advance();
                if (std::find(parameters.begin(), parameters.end(), local.text) != parameters.end() ||
                    std::find(locals.begin(), locals.end(), local.text) != locals.end())
                    error(local, "duplicate local or parameter name");
                locals.push_back(local.text);
            } while (match(Kind::Comma));
            if (!match(Kind::Newline)) error(peek(), "expected end of line after LOCAL");
            while (match(Kind::Newline)) {}
        }

        inDefinition_ = true;
        block();
        inDefinition_ = false;
        if (!match(Kind::EndKeyword)) error(peek(), "expected END DEFINE");
        if (!match(Kind::Define)) error(peek(), "expected DEFINE after END");
        if (check(Kind::Identifier)) {
            const Token endName = advance();
            if (endName.text != name.text) error(endName, "END DEFINE name does not match definition");
        }
        Instruction finish{isFunction ? Op::FunctionEnd : Op::Return,
                           0.0, name.text, defineKeyword.line, defineKeyword.column};
        code_.push_back(std::move(finish));
        code_[skipIndex].target = code_.size();
        definitions_[name.text].locals = std::move(locals);
        if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
            error(peek(), "expected end of statement");
    }
    void blockFor() {
        const Token keyword = advance();
        if (!check(Kind::Identifier)) error(peek(), "expected numeric FOR variable");
        const Token variable = advance();
        if (!variable.text.empty() && variable.text.back() == '$')
            error(variable, "FOR variable must be numeric");
        if (!match(Kind::Equal)) error(peek(), "expected '=' after FOR variable");

        std::vector<ForItemSpec> items;
        while (true) {
            expression();
            ForItemSpec item;
            if (match(Kind::To)) {
                item.hasEnd = true;
                expression();
                if (match(Kind::Step)) {
                    item.hasStep = true;
                    expression();
                }
            }
            items.push_back(item);
            if (!match(Kind::Comma)) break;
        }

        const std::size_t initIndex = code_.size();
        Instruction init{Op::ForInit, 0.0, variable.text, keyword.line, keyword.column};
        init.loopId = nextLoopId_++;
        init.forItems = std::move(items);
        code_.push_back(std::move(init));
        const std::size_t body = code_.size();
        loops_.push_back({code_[initIndex].loopId, variable.text, body, true, {}, {}});

        if (match(Kind::Colon)) {
            // Inline bodies end at NEXT. Nested inline loops are deliberately excluded.
            while (!check(Kind::Next) && !check(Kind::End) && !check(Kind::Newline)) {
                if (match(Kind::Colon)) continue;
                if (check(Kind::For) || check(Kind::Repeat))
                    error(peek(), "nested inline loops are not supported");
                statement();
                if (!check(Kind::Next) && !check(Kind::Newline) && !check(Kind::End) &&
                    !match(Kind::Colon))
                    error(peek(), "expected ':' or NEXT in inline FOR");
            }
            if (!match(Kind::Next)) error(peek(), "expected NEXT for inline FOR");
            const Token nextToken = previous();
            if (!check(Kind::Identifier)) error(peek(), "expected variable after NEXT");
            const Token nextVariable = advance();
            if (nextVariable.text != variable.text)
                error(nextVariable, "NEXT variable does not match FOR");
            emitForNext(nextToken, nextVariable);
            code_[initIndex].target = code_.size();
            for (const std::size_t index : loops_.back().nextInstructions)
                code_[index].exitTarget = code_.size();
            for (const std::size_t index : loops_.back().exitInstructions)
                code_[index].target = code_.size();
            loops_.pop_back();
            return;
        }

        if (!match(Kind::Newline)) error(peek(), "expected end of line or ':' after FOR header");
        while (match(Kind::Newline)) {}
        block();
        if (!match(Kind::EndKeyword)) error(peek(), "expected END FOR");
        if (!match(Kind::For)) error(peek(), "expected FOR after END");
        if (!check(Kind::Identifier)) error(peek(), "expected variable after END FOR");
        const Token endVariable = advance();
        if (endVariable.text != variable.text)
            error(endVariable, "END FOR variable does not match FOR");
        emitForNext(endVariable, endVariable);
        code_[initIndex].target = code_.size();
        for (const std::size_t index : loops_.back().nextInstructions)
            code_[index].exitTarget = code_.size();
        for (const std::size_t index : loops_.back().exitInstructions)
            code_[index].target = code_.size();
        loops_.pop_back();
        if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
            error(peek(), "expected end of statement");
    }
    void blockRepeat() {
        const Token keyword = advance();
        if (!check(Kind::Identifier)) error(peek(), "expected REPeat loop name");
        const Token name = advance();
        const std::size_t loopId = nextLoopId_++;
        const std::size_t body = code_.size();
        loops_.push_back({loopId, name.text, body, false, {}, {}});

        if (match(Kind::Colon)) {
            while (!check(Kind::End) && !check(Kind::Newline)) {
                if (match(Kind::Colon)) continue;
                if (check(Kind::For) || check(Kind::Repeat))
                    error(peek(), "nested inline loops are not supported");
                statement();
                if (!check(Kind::End) && !check(Kind::Newline) && !match(Kind::Colon))
                    error(peek(), "expected ':' or end of line in inline REPeat");
            }
            Instruction jump{Op::Jump, 0.0, name.text, keyword.line, keyword.column};
            jump.target = body;
            code_.push_back(std::move(jump));
            const std::size_t exit = code_.size();
            for (const std::size_t index : loops_.back().exitInstructions)
                code_[index].target = exit;
            loops_.pop_back();
            return;
        }

        if (!match(Kind::Newline)) error(peek(), "expected ':' or end of line after REPeat name");
        while (match(Kind::Newline)) {}
        block();
        if (!match(Kind::EndKeyword)) error(peek(), "expected END REPeat");
        if (!match(Kind::Repeat)) error(peek(), "expected REPeat after END");
        if (!check(Kind::Identifier)) error(peek(), "expected name after END REPeat");
        const Token endName = advance();
        if (endName.text != name.text) error(endName, "END REPeat name does not match REPeat");
        Instruction jump{Op::Jump, 0.0, name.text, keyword.line, keyword.column};
        jump.target = body;
        code_.push_back(std::move(jump));
        const std::size_t exit = code_.size();
        for (const std::size_t index : loops_.back().exitInstructions)
            code_[index].target = exit;
        loops_.pop_back();
        if (!check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End))
            error(peek(), "expected end of statement");
    }
    void statement() {
        if (check(Kind::If)) { blockIf(); return; }
        if (check(Kind::For)) { blockFor(); return; }
        if (check(Kind::Repeat)) { blockRepeat(); return; }
        if (check(Kind::Define)) { blockDefinition(); return; }
        if (check(Kind::Dim)) { blockDim(); return; }
        if (check(Kind::Select)) { blockSelect(); return; }
        if (match(Kind::Return)) {
            const Token keyword = previous();
            if (!inDefinition_) error(keyword, "RETURN is only valid inside a definition");
            const bool hasValue = !check(Kind::Newline) && !check(Kind::Colon) && !check(Kind::End);
            if (hasValue) expression();
            Instruction instruction{Op::Return, 0.0, {}, keyword.line, keyword.column};
            instruction.expectsValue = hasValue;
            code_.push_back(std::move(instruction));
            return;
        }
        if (match(Kind::Local)) error(previous(), "LOCAL must follow a definition header");
        if (match(Kind::Next)) {
            const Token keyword = previous();
            if (!check(Kind::Identifier)) error(peek(), "expected variable after NEXT");
            const Token variable = advance();
            emitForNext(keyword, variable, true);
            return;
        }
        if (match(Kind::Exit)) {
            const Token keyword = previous();
            if (!check(Kind::Identifier)) error(peek(), "expected loop name after EXIT");
            const Token name = advance();
            emitExit(keyword, name);
            return;
        }
        if (check(Kind::Identifier) && arrays_.contains(peek().text) &&
            current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == Kind::LeftParen) {
            const Token name = advance();
            const std::size_t rank = callArguments();
            if (rank != arrays_[name.text]) error(name, "incorrect array rank");
            if (!match(Kind::Equal)) error(peek(), "expected '=' after array index");
            expression();
            Instruction instruction{Op::ArraySet, 0.0, name.text, name.line, name.column};
            instruction.rank = rank;
            code_.push_back(std::move(instruction));
            return;
        }
        if (check(Kind::Identifier) && current_ + 1 < tokens_.size() &&
            tokens_[current_ + 1].kind == Kind::LeftParen) {
            const Token name = advance();
            const std::size_t arguments = callArguments();
            emitCall(name, arguments, false);
            return;
        }
        if (match(Kind::Print)) {
            const Token keyword = previous();
            if (check(Kind::Newline) || check(Kind::Colon) || check(Kind::End)) {
                emit(Op::PrintEnd, keyword);
                return;
            }
            PrintSeparator preceding = PrintSeparator::None;
            while (true) {
                expression();
                emit(Op::Print, keyword, 0.0, {}, preceding);
                if (!isPrintSeparator(peek().kind)) break;
                const Token separator = advance();
                preceding = separatorKind(separator.kind);
                if (check(Kind::Newline) || check(Kind::Colon) || check(Kind::End)) {
                    emit(Op::PrintSeparator, separator, 0.0, {}, preceding);
                    return;
                }
            }
            emit(Op::PrintEnd, keyword);
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
    static bool isPrintSeparator(Kind kind) {
        return kind == Kind::Semicolon || kind == Kind::Exclamation ||
               kind == Kind::Comma || kind == Kind::Backslash;
    }
    static PrintSeparator separatorKind(Kind kind) {
        switch (kind) {
        case Kind::Semicolon: return PrintSeparator::Semicolon;
        case Kind::Exclamation: return PrintSeparator::Exclamation;
        case Kind::Comma: return PrintSeparator::Comma;
        case Kind::Backslash: return PrintSeparator::Backslash;
        default: return PrintSeparator::None;
        }
    }
    void expression() { logicalOr(); }
    void logicalOr() {
        logicalAnd();
        while (match(Kind::Or) || match(Kind::Xor)) {
            const Token op = previous(); logicalAnd();
            emit(op.kind == Kind::Or ? Op::Or : Op::Xor, op);
        }
    }
    void logicalAnd() {
        logicalNot();
        while (match(Kind::And)) {
            const Token op = previous(); logicalNot(); emit(Op::And, op);
        }
    }
    void logicalNot() {
        if (match(Kind::Not)) { const Token op = previous(); logicalNot(); emit(Op::Not, op); }
        else comparison();
    }
    void comparison() {
        additive();
        if (match(Kind::Equal) || match(Kind::NotEqual) || match(Kind::Less) ||
            match(Kind::LessEqual) || match(Kind::Greater) || match(Kind::GreaterEqual)) {
            const Token op = previous(); additive();
            Op instruction = Op::Equal;
            switch (op.kind) {
            case Kind::NotEqual: instruction = Op::NotEqual; break;
            case Kind::Less: instruction = Op::Less; break;
            case Kind::LessEqual: instruction = Op::LessEqual; break;
            case Kind::Greater: instruction = Op::Greater; break;
            case Kind::GreaterEqual: instruction = Op::GreaterEqual; break;
            default: break;
            }
            emit(instruction, op);
            if (check(Kind::Equal) || check(Kind::NotEqual) || check(Kind::Less) ||
                check(Kind::LessEqual) || check(Kind::Greater) || check(Kind::GreaterEqual))
                error(peek(), "chained comparisons are not supported");
        }
    }
    void additive() {
        term(); while (match(Kind::Plus) || match(Kind::Minus)) {
            const Token op = previous(); term(); emit(op.kind == Kind::Plus ? Op::Add : Op::Subtract, op);
        }
    }
    void term() {
        power();
        while (match(Kind::Star) || match(Kind::Slash) || match(Kind::Div) || match(Kind::Mod)) {
            const Token op = previous(); power();
            Op instruction = Op::Multiply;
            if (op.kind == Kind::Slash) instruction = Op::Divide;
            else if (op.kind == Kind::Div) instruction = Op::IntegerDivide;
            else if (op.kind == Kind::Mod) instruction = Op::Modulo;
            emit(instruction, op);
        }
    }
    void power() {
        signedPrimary();
        while (match(Kind::Power)) {
            const Token op = previous(); signedPrimary(); emit(Op::Power, op);
        }
    }
    void signedPrimary() {
        if (match(Kind::Minus)) { const Token op = previous(); signedPrimary(); emit(Op::Negate, op); }
        else if (match(Kind::Plus)) signedPrimary();
        else primary();
    }
    void primary() {
        if (match(Kind::Number)) { const Token t = previous(); emit(Op::Push, t, t.number); return; }
        if (match(Kind::String)) { const Token t = previous(); emit(Op::Push, t, t.text); return; }
        if (match(Kind::Identifier)) {
            const Token t = previous();
            if (check(Kind::LeftParen)) {
                const std::size_t arguments = callArguments();
                emitCall(t, arguments, true);
            } else {
                emit(Op::Load, t, 0.0, t.text);
            }
            return;
        }
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

[[noreturn]] void runtimeError(const Instruction& at, const char* message) {
    throw std::runtime_error("line " + std::to_string(at.line) + ", column " +
                             std::to_string(at.column) + ": " + message);
}

std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (const unsigned char byte : text)
        if ((byte & 0xc0) != 0x80) ++count;
    return count;
}

void writeText(const std::string& text, std::ostream& output, std::size_t& column) {
    output << text;
    column += utf8Length(text);
}

void printValue(const Value& value, std::ostream& output, std::size_t& column) {
    if (const auto* number = std::get_if<double>(&value)) {
        if (*number == 0) {
            writeText("0", output, column);
        } else {
            char buffer[64];
            const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), *number);
            if (converted.ec != std::errc{}) throw std::runtime_error("numeric formatting failed");
            writeText(std::string(buffer, converted.ptr), output, column);
        }
    } else {
        writeText(std::get<std::string>(value), output, column);
    }
}

void printSeparator(PrintSeparator separator, std::ostream& output, std::size_t& column) {
    switch (separator) {
    case PrintSeparator::None:
    case PrintSeparator::Semicolon:
        break;
    case PrintSeparator::Exclamation:
        if (column != 0) writeText(" ", output, column);
        break;
    case PrintSeparator::Comma: {
        const std::size_t spaces = 8 - (column % 8);
        writeText(std::string(spaces, ' '), output, column);
        break;
    }
    case PrintSeparator::Backslash:
        output << '\n';
        column = 0;
        break;
    }
}

} // namespace

void Interpreter::execute(const std::string& source, std::ostream& output) {
    Compiler compiler(Lexer(source).scan());
    const auto code = compiler.compile();
    std::vector<Value> stack;
    std::unordered_map<std::size_t, ForState> forStates;
    std::vector<RuntimeFrame> callFrames;
    std::unordered_map<std::string, RuntimeArray> arrays;
    std::vector<SelectState> selectStates;
    auto pop = [&]() {
        if (stack.empty()) throw std::runtime_error("internal VM stack underflow");
        Value value = std::move(stack.back());
        stack.pop_back();
        return value;
    };
    auto assignLoopValue = [&](const Instruction& instruction, double value) {
        if (!std::isfinite(value)) runtimeError(instruction, "numeric overflow");
        if (!instruction.name.empty() && instruction.name.back() == '%' &&
            (std::trunc(value) != value ||
             value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
             value > static_cast<double>(std::numeric_limits<std::int32_t>::max())))
            runtimeError(instruction, "Int32 value out of range");
        variables_[instruction.name] = value;
    };
    auto rangeContains = [](const ForRange& range, double value) {
        if (range.single) return true;
        return range.step > 0 ? value <= range.end : value >= range.end;
    };
    auto arrayOffset = [&](const Instruction& instruction) {
        const auto found = arrays.find(instruction.name);
        if (found == arrays.end()) runtimeError(instruction, "array is not dimensioned");
        RuntimeArray& array = found->second;
        if (array.lengths.size() != instruction.rank)
            runtimeError(instruction, "incorrect array rank");
        std::vector<std::size_t> indices(instruction.rank);
        for (std::size_t i = instruction.rank; i-- > 0;) {
            const double index = numeric(pop(), instruction);
            if (std::trunc(index) != index || index < 0 || index > 2147483647.0)
                runtimeError(instruction, "array index must be a nonnegative integer");
            indices[i] = static_cast<std::size_t>(index);
        }
        std::size_t offset = 0;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (indices[i] >= array.lengths[i]) runtimeError(instruction, "array index out of bounds");
            offset = offset * array.lengths[i] + indices[i];
        }
        return std::pair<RuntimeArray*, std::size_t>{&array, offset};
    };

    std::size_t pc = 0;
    while (pc < code.size()) {
        const auto& instruction = code[pc++];
        switch (instruction.op) {
        case Op::Call: {
            if (callFrames.size() >= 256) runtimeError(instruction, "call stack limit exceeded");
            std::vector<Value> arguments(instruction.argCount);
            for (std::size_t i = instruction.argCount; i-- > 0;)
                arguments[i] = pop();
            RuntimeFrame frame;
            frame.returnAddress = pc;
            frame.function = instruction.expectsValue;
            for (std::size_t i = 0; i < instruction.parameters.size(); ++i)
                frame.locals[instruction.parameters[i]] = std::move(arguments[i]);
            for (const std::string& local : instruction.localNames) {
                if (!frame.locals.contains(local)) {
                    if (!local.empty() && local.back() == '$') frame.locals[local] = std::string{};
                    else frame.locals[local] = 0.0;
                }
            }
            callFrames.push_back(std::move(frame));
            pc = instruction.target;
            break;
        }
        case Op::Return: {
            if (callFrames.empty()) runtimeError(instruction, "RETURN outside a definition");
            RuntimeFrame& frame = callFrames.back();
            Value result = 0.0;
            if (instruction.expectsValue) {
                if (!frame.function) runtimeError(instruction, "procedure cannot return a value");
                result = pop();
            } else if (frame.function) {
                runtimeError(instruction, "function must return a value");
            }
            pc = frame.returnAddress;
            const bool returnsValue = frame.function;
            callFrames.pop_back();
            if (returnsValue) stack.push_back(std::move(result));
            break;
        }
        case Op::FunctionEnd:
            runtimeError(instruction, "function did not return a value");
        case Op::DimArray: {
            std::vector<std::size_t> lengths(instruction.rank);
            std::size_t elements = 1;
            for (std::size_t i = instruction.rank; i-- > 0;) {
                const double upper = numeric(pop(), instruction);
                if (std::trunc(upper) != upper || upper < 0 || upper > 2147483647.0)
                    runtimeError(instruction, "array upper bound must be a nonnegative integer");
                const std::size_t length = static_cast<std::size_t>(upper) + 1;
                if (elements > 1000000 / length)
                    runtimeError(instruction, "array allocation limit exceeded");
                elements *= length;
                lengths[i] = length;
            }
            RuntimeArray array;
            array.lengths = std::move(lengths);
            if (!instruction.name.empty() && instruction.name.back() == '$')
                array.values.resize(elements, std::string{});
            else
                array.values.resize(elements, 0.0);
            arrays[instruction.name] = std::move(array);
            break;
        }
        case Op::ArrayGet: {
            auto [array, offset] = arrayOffset(instruction);
            stack.push_back(array->values[offset]);
            break;
        }
        case Op::ArraySet: {
            Value value = pop();
            auto [array, offset] = arrayOffset(instruction);
            if (!instruction.name.empty() && instruction.name.back() == '$') {
                if (!std::holds_alternative<std::string>(value))
                    runtimeError(instruction, "string value required for array element");
            } else {
                const double number = numeric(value, instruction);
                if (!instruction.name.empty() && instruction.name.back() == '%' &&
                    (std::trunc(number) != number ||
                     number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                     number > static_cast<double>(std::numeric_limits<std::int32_t>::max())))
                    runtimeError(instruction, "Int32 array element out of range");
            }
            array->values[offset] = std::move(value);
            break;
        }
        case Op::SelectBegin: {
            const double value = numeric(pop(), instruction);
            selectStates.push_back({value, false});
            break;
        }
        case Op::SelectMatch: {
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            bool matched = false;
            for (std::size_t i = instruction.selectItems.size(); i-- > 0;) {
                const SelectItemSpec& item = instruction.selectItems[i];
                double end = 0;
                if (item.hasEnd) end = numeric(pop(), instruction);
                const double start = numeric(pop(), instruction);
                const double selected = selectStates.back().value;
                const bool thisMatch = item.hasEnd ? (selected >= start && selected <= end)
                                                   : (selected == start);
                matched = matched || thisMatch;
            }
            if (matched) selectStates.back().matched = true;
            stack.emplace_back(matched ? 1.0 : 0.0);
            break;
        }
        case Op::SelectRemainder: {
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            const bool matched = !selectStates.back().matched;
            if (matched) selectStates.back().matched = true;
            stack.emplace_back(matched ? 1.0 : 0.0);
            break;
        }
        case Op::SelectEnd:
            if (selectStates.empty()) runtimeError(instruction, "inactive SELECT block");
            selectStates.pop_back();
            break;
        case Op::ForInit: {
            ForState state;
            state.ranges.resize(instruction.forItems.size());
            for (std::size_t i = instruction.forItems.size(); i-- > 0;) {
                const ForItemSpec& spec = instruction.forItems[i];
                ForRange& range = state.ranges[i];
                if (spec.hasStep) range.step = numeric(pop(), instruction);
                if (spec.hasEnd) range.end = numeric(pop(), instruction);
                range.start = numeric(pop(), instruction);
                range.single = !spec.hasEnd;
                if (spec.hasEnd && range.step == 0)
                    runtimeError(instruction, "FOR step cannot be zero");
            }
            while (state.item < state.ranges.size() &&
                   !rangeContains(state.ranges[state.item], state.ranges[state.item].start))
                ++state.item;
            if (state.item == state.ranges.size()) {
                pc = instruction.target;
                break;
            }
            const double initial = state.ranges[state.item].start;
            assignLoopValue(instruction, initial);
            forStates[instruction.loopId] = std::move(state);
            break;
        }
        case Op::ForNext: {
            auto found = forStates.find(instruction.loopId);
            if (found == forStates.end()) runtimeError(instruction, "inactive FOR loop");
            ForState& state = found->second;
            const auto variable = variables_.find(instruction.name);
            if (variable == variables_.end()) runtimeError(instruction, "missing FOR variable");
            const double current = numeric(variable->second, instruction);
            const ForRange& range = state.ranges[state.item];
            const double next = range.single ? current : current + range.step;
            if (!range.single && !std::isfinite(next))
                runtimeError(instruction, "numeric overflow");
            if (!range.single && rangeContains(range, next)) {
                assignLoopValue(instruction, next);
                for (auto loop = forStates.begin(); loop != forStates.end();) {
                    if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                    else ++loop;
                }
                pc = instruction.target;
                break;
            }
            ++state.item;
            while (state.item < state.ranges.size() &&
                   !rangeContains(state.ranges[state.item], state.ranges[state.item].start))
                ++state.item;
            if (state.item == state.ranges.size()) {
                if (!instruction.explicitNext) {
                    forStates.erase(found);
                    pc = instruction.exitTarget;
                } else {
                    --state.item;
                }
                break;
            }
            assignLoopValue(instruction, state.ranges[state.item].start);
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        }
        case Op::LoopNext:
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first > instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        case Op::ExitLoop:
            for (auto loop = forStates.begin(); loop != forStates.end();) {
                if (loop->first >= instruction.loopId) loop = forStates.erase(loop);
                else ++loop;
            }
            pc = instruction.target;
            break;
        case Op::JumpIfFalse:
            if (numeric(pop(), instruction) == 0) pc = instruction.target;
            break;
        case Op::Jump:
            pc = instruction.target;
            break;
        case Op::Push: stack.push_back(instruction.value); break;
        case Op::Load: {
            if (!callFrames.empty()) {
                const auto local = callFrames.back().locals.find(instruction.name);
                if (local != callFrames.back().locals.end()) {
                    stack.push_back(local->second);
                    break;
                }
            }
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
            if (!callFrames.empty() && callFrames.back().locals.contains(instruction.name))
                callFrames.back().locals[instruction.name] = std::move(value);
            else
                variables_[instruction.name] = std::move(value);
            break;
        }
        case Op::Negate: {
            const double value = -numeric(pop(), instruction);
            if (!std::isfinite(value)) runtimeError(instruction, "numeric overflow");
            stack.push_back(value);
            break;
        }
        case Op::Not: {
            const double value = numeric(pop(), instruction);
            stack.emplace_back(value == 0 ? 1.0 : 0.0);
            break;
        }
        case Op::Add: case Op::Subtract: case Op::Multiply: case Op::Divide:
        case Op::IntegerDivide: case Op::Modulo: case Op::Power: {
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
            if ((instruction.op == Op::Divide || instruction.op == Op::IntegerDivide ||
                 instruction.op == Op::Modulo) && b == 0)
                runtimeError(instruction, "division by zero");
            if (instruction.op == Op::Power && a == 0 && b < 0)
                runtimeError(instruction, "division by zero");
            if (instruction.op == Op::Power && a < 0 && std::trunc(b) != b)
                runtimeError(instruction, "invalid exponent");
            double result = 0;
            if (instruction.op == Op::Add) result = a + b;
            else if (instruction.op == Op::Subtract) result = a - b;
            else if (instruction.op == Op::Multiply) result = a * b;
            else if (instruction.op == Op::Divide) result = a / b;
            else if (instruction.op == Op::Power) result = std::pow(a, b);
            else {
                if (std::trunc(a) != a || std::trunc(b) != b ||
                    a < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    a > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
                    b < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    b > static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                    runtimeError(instruction, "Int32 operands required");
                const auto lhs = static_cast<std::int64_t>(a);
                const auto rhs = static_cast<std::int64_t>(b);
                std::int64_t quotient = lhs / rhs;
                std::int64_t remainder = lhs % rhs;
                if (remainder != 0 && ((lhs < 0) != (rhs < 0))) {
                    --quotient;
                    remainder += rhs;
                }
                if (instruction.op == Op::IntegerDivide) {
                    if (quotient < std::numeric_limits<std::int32_t>::min() ||
                        quotient > std::numeric_limits<std::int32_t>::max())
                        runtimeError(instruction, "Int32 result out of range");
                    result = static_cast<double>(quotient);
                } else {
                    result = static_cast<double>(remainder);
                }
            }
            if (!std::isfinite(result)) runtimeError(instruction, "numeric overflow");
            stack.push_back(result);
            break;
        }
        case Op::Equal: case Op::NotEqual: case Op::Less: case Op::LessEqual:
        case Op::Greater: case Op::GreaterEqual: {
            const Value right = pop();
            const Value left = pop();
            bool result = false;
            const bool strings = std::holds_alternative<std::string>(left) &&
                                 std::holds_alternative<std::string>(right);
            if (instruction.op == Op::Equal || instruction.op == Op::NotEqual) {
                if (strings) result = std::get<std::string>(left) == std::get<std::string>(right);
                else if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right))
                    result = std::get<double>(left) == std::get<double>(right);
                else runtimeError(instruction, "values must have matching types");
                if (instruction.op == Op::NotEqual) result = !result;
            } else {
                if (strings) runtimeError(instruction, "relational comparison requires numbers");
                const double a = numeric(left, instruction);
                const double b = numeric(right, instruction);
                switch (instruction.op) {
                case Op::Less: result = a < b; break;
                case Op::LessEqual: result = a <= b; break;
                case Op::Greater: result = a > b; break;
                case Op::GreaterEqual: result = a >= b; break;
                default: break;
                }
            }
            stack.emplace_back(result ? 1.0 : 0.0);
            break;
        }
        case Op::And: case Op::Or: case Op::Xor: {
            const double right = numeric(pop(), instruction);
            const double left = numeric(pop(), instruction);
            const bool a = left != 0;
            const bool b = right != 0;
            const bool result = instruction.op == Op::And ? (a && b) :
                                instruction.op == Op::Or ? (a || b) : (a != b);
            stack.emplace_back(result ? 1.0 : 0.0);
            break;
        }
        case Op::Print:
            printSeparator(instruction.separator, output, output_column_);
            printValue(pop(), output, output_column_);
            break;
        case Op::PrintSeparator:
            printSeparator(instruction.separator, output, output_column_);
            break;
        case Op::PrintEnd:
            output << '\n';
            output_column_ = 0;
            break;
        }
    }
}

void Interpreter::executeInteractive(const std::string& source, std::ostream& output) {
    auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r");
        if (first == std::string::npos) return std::string{};
        const auto last = value.find_last_not_of(" \t\r");
        return value.substr(first, last - first + 1);
    };
    auto command = [&](const std::string& line) {
        std::string value = trim(line);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return value;
    };
    auto lineNumberEnd = [](const std::string& line) -> std::size_t {
        std::size_t i = 0;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
        if (i == 0 || (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r'))
            return 0;
        return i;
    };

    bool commandMode = false;
    std::istringstream probe(source);
    for (std::string line; std::getline(probe, line);) {
        const std::size_t numberEnd = lineNumberEnd(line);
        const std::string word = command(line);
        if (numberEnd != 0 || word == "RUN" || word == "LIST" || word == "NEW") {
            commandMode = true;
            break;
        }
    }
    if (!commandMode) {
        execute(source, output);
        return;
    }

    std::istringstream input(source);
    for (std::string line; std::getline(input, line);) {
        const std::size_t numberEnd = lineNumberEnd(line);
        if (numberEnd != 0) {
            int number = 0;
            const auto parsed = std::from_chars(line.data(), line.data() + numberEnd, number);
            if (parsed.ec != std::errc{} || number < 1 || number > 32767)
                throw std::runtime_error("program line number must be in 1..32767");
            std::string body = trim(line.substr(numberEnd));
            if (body.empty()) program_.erase(number);
            else program_[number] = std::move(body);
            continue;
        }

        const std::string word = command(line);
        if (word.empty()) continue;
        if (word == "LIST") {
            for (const auto& [number, body] : program_)
                output << number << (body.empty() ? "" : " " + body) << '\n';
            output_column_ = 0;
        } else if (word == "NEW") {
            program_.clear();
            clear();
        } else if (word == "RUN") {
            clear();
            std::string programSource;
            for (const auto& [number, body] : program_) {
                (void)number;
                programSource += body;
                programSource.push_back('\n');
            }
            execute(programSource, output);
        } else {
            execute(line + "\n", output);
        }
    }
}

void Interpreter::clear() { variables_.clear(); output_column_ = 0; }

} // namespace ql32
