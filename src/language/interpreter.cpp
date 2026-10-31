#include "ql32/interpreter.hpp"
#include "bytecode.hpp"
#include "../vm/execution_state.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ql32 {
namespace {
std::atomic<runtime::SessionId> nextInterpreterSessionId{1};

using detail::ArrayArgumentCandidate;
using detail::ForItemSpec;
using detail::Instruction;
using detail::Op;
using detail::PrintSeparator;
using detail::SelectItemSpec;

enum class Kind {
    End, Newline, Number, String, Identifier, Print, If, Then, Else, EndKeyword, Dim,
    Select, On, Remainder, Remark,
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
    std::size_t sourceStart = 0;
    std::size_t sourceLength = 0;
};

struct Keyword {
    std::string_view spelling;
    std::string_view mandatory;
    Kind kind;
    std::string_view canonical;
};

constexpr Keyword keywords[] = {
    {"PRINT", "PRINT", Kind::Print, "PRINT"}, {"DIM", "DIM", Kind::Dim, "DIM"},
    {"SELECT", "SEL", Kind::Select, "SELect"}, {"ON", "ON", Kind::On, "ON"},
    {"REMARK", "REM", Kind::Remark, "REMark"},
    {"REMAINDER", "REMAINDER", Kind::Remainder, "REMAINDER"},
    {"IF", "IF", Kind::If, "IF"}, {"THEN", "THEN", Kind::Then, "THEN"},
    {"ELSE", "ELSE", Kind::Else, "ELSE"}, {"END", "END", Kind::EndKeyword, "END"},
    {"FOR", "FOR", Kind::For, "FOR"}, {"TO", "TO", Kind::To, "TO"},
    {"STEP", "STEP", Kind::Step, "STEP"}, {"NEXT", "NEXT", Kind::Next, "NEXT"},
    {"REPEAT", "REP", Kind::Repeat, "REPeat"}, {"EXIT", "EXIT", Kind::Exit, "EXIT"},
    {"DEFINE", "DEF", Kind::Define, "DEFine"},
    {"PROCEDURE", "PROC", Kind::Procedure, "PROCedure"},
    {"FUNCTION", "FN", Kind::Function, "FuNction"},
    {"LOCAL", "LOC", Kind::Local, "LOCal"}, {"RETURN", "RET", Kind::Return, "RETurn"},
    {"DIV", "DIV", Kind::Div, "DIV"}, {"MOD", "MOD", Kind::Mod, "MOD"},
    {"AND", "AND", Kind::And, "AND"}, {"OR", "OR", Kind::Or, "OR"},
    {"XOR", "XOR", Kind::Xor, "XOR"}, {"NOT", "NOT", Kind::Not, "NOT"}
};

bool isBuiltinName(const std::string& name) {
    return name == "LEN" || name == "OPEN" || name == "READ$" || name == "EOF" ||
           name == "WRITE" || name == "CLOSE" || name == "ALERT" || name == "CONFIRM" ||
           name == "DINIT" || name == "DTEXT" || name == "DEDIT" || name == "DINT" ||
           name == "DFLOAT" || name == "DCHOICE" || name == "DCHECK" ||
           name == "DBUTTONS" || name == "DIALOG" || name == "DGET" ||
           name == "DGET$" || name == "MINIT" || name == "MCARD" || name == "MENU" ||
           name == "INK" || name == "PAPER" || name == "LINE" || name == "RECT" ||
           name == "CIRCLE" || name == "TEXT" || name == "CLS";
}

std::pair<std::size_t, bool> builtinSignature(const Instruction& instruction) {
    if (instruction.name == "LEN") return {1, true};
    if (instruction.name == "ALERT") return {2, false};
    if (instruction.name == "CONFIRM") return {2, true};
    if (instruction.name == "OPEN") return {2, true};
    if (instruction.name == "READ$" || instruction.name == "EOF") return {1, true};
    if (instruction.name == "WRITE") return {2, false};
    if (instruction.name == "CLOSE") return {1, false};
    if (instruction.name == "DINIT" || instruction.name == "MINIT") return {1, true};
    if (instruction.name == "DTEXT") return {2, false};
    if (instruction.name == "DEDIT" || instruction.name == "DCHECK") return {3, false};
    if (instruction.name == "DINT" || instruction.name == "DFLOAT") return {5, false};
    if (instruction.name == "DCHOICE") return {4, false};
    if (instruction.name == "DBUTTONS") return {3, false};
    if (instruction.name == "DIALOG" || instruction.name == "MENU") return {1, true};
    if (instruction.name == "DGET" || instruction.name == "DGET$") return {2, true};
    if (instruction.name == "MCARD") return {4, false};
    if (instruction.name == "INK" || instruction.name == "PAPER") return {1, false};
    if (instruction.name == "LINE" || instruction.name == "RECT") return {4, false};
    if (instruction.name == "CIRCLE") return {3, false};
    if (instruction.name == "TEXT") return {3, false};
    if (instruction.name == "CLS") return {0, false};
    return {0, false};
}

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
        errno = 0;
        char* parsedEnd = nullptr;
        const double value = std::strtod(text.c_str(), &parsedEnd);
        if (errno == ERANGE || parsedEnd != text.c_str() + text.size() ||
            !std::isfinite(value))
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
        const Keyword* matched = nullptr;
        for (const Keyword& keyword : keywords) {
            std::size_t inputIndex = 0;
            bool subsequence = true;
            for (const char expected : keyword.spelling) {
                if (inputIndex < upper.size() && upper[inputIndex] == expected) ++inputIndex;
            }
            subsequence = inputIndex == upper.size();
            std::size_t mandatoryIndex = 0;
            bool containsMandatory = true;
            for (const char expected : upper) {
                if (mandatoryIndex < keyword.mandatory.size() &&
                    expected == keyword.mandatory[mandatoryIndex]) ++mandatoryIndex;
            }
            containsMandatory = mandatoryIndex == keyword.mandatory.size();
            if (subsequence && containsMandatory) {
                if (matched != nullptr)
                    fail("ambiguous keyword abbreviation");
                matched = &keyword;
            }
        }
        if (matched != nullptr && matched->spelling == "REMARK") {
            skipComment();
            return {Kind::Newline, "", 0, line_, column};
        }
        if (matched != nullptr)
            return {matched->kind, std::string(matched->canonical), 0, line_, column,
                    start, text.size()};
        return {Kind::Identifier, upper, 0, line_, column, start, text.size()};
    }
};

std::string canonicalizeKeywords(const std::string& source) {
    const std::vector<Token> tokens = Lexer(source).scan();
    std::string result;
    std::size_t copiedThrough = 0;
    for (const Token& token : tokens) {
        if (token.sourceLength == 0 || token.sourceStart < copiedThrough) continue;
        const auto keyword = std::find_if(std::begin(keywords), std::end(keywords),
            [&](const Keyword& candidate) {
                return candidate.kind == token.kind && candidate.kind != Kind::Remark;
            });
        if (keyword == std::end(keywords)) continue;
        result.append(source, copiedThrough, token.sourceStart - copiedThrough);
        result.append(keyword->canonical);
        copiedThrough = token.sourceStart + token.sourceLength;
    }
    result.append(source, copiedThrough, std::string::npos);
    return result;
}

struct DefinitionInfo {
    std::size_t address = 0;
    bool function = false;
    std::vector<std::string> parameters;
    std::vector<std::string> locals;
    std::vector<std::size_t> parameterArrayRanks;
};

class Compiler {
public:
    explicit Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
        for (std::size_t i = 0; i + 2 < tokens_.size(); ++i) {
            if (tokens_[i].kind == Kind::Define && tokens_[i + 1].kind == Kind::Function &&
                tokens_[i + 2].kind == Kind::Identifier)
                declaredFunctions_.insert(tokens_[i + 2].text);
        }
    }

    std::vector<Instruction> compile(bool tolerateUnresolvedCalls = false,
                                     bool tolerateIncompleteBlocks = false) {
        try {
            block();
            if (!check(Kind::End)) error(peek(), "unexpected block terminator");
        } catch (const std::runtime_error& error) {
            const std::string message = error.what();
            const bool incomplete = message.find("expected END IF") != std::string::npos ||
                message.find("expected END FOR") != std::string::npos ||
                message.find("expected END REPeat") != std::string::npos ||
                message.find("expected END DEFINE") != std::string::npos ||
                message.find("expected END SELECT") != std::string::npos ||
                message.find("SELECT requires at least one arm") != std::string::npos;
            if (!tolerateIncompleteBlocks || !incomplete || !check(Kind::End)) throw;
            return {};
        }
        resolveCalls(tolerateUnresolvedCalls);
        return std::move(code_);
    }

private:
    std::vector<Token> tokens_;
    std::vector<Instruction> code_;
    std::unordered_map<std::string, DefinitionInfo> definitions_;
    std::unordered_map<std::string, std::size_t> arrays_;
    std::unordered_set<std::string> declaredFunctions_;
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
    std::string currentDefinitionName_;

    bool isCurrentParameter(const std::string& name) const {
        const auto definition = definitions_.find(currentDefinitionName_);
        return definition != definitions_.end() &&
               std::find(definition->second.parameters.begin(),
                         definition->second.parameters.end(), name) !=
                   definition->second.parameters.end();
    }

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
                 current_ + 2 < tokens_.size() &&
                 (tokens_[current_ + 2].kind == Kind::Equal ||
                  tokens_[current_ + 2].kind == Kind::LeftParen)));
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
    void resolveCalls(bool tolerateUnresolvedCalls) {
        for (Instruction& instruction : code_) {
            if (instruction.op != Op::Call) continue;
            if (isBuiltinName(instruction.name)) {
                const auto [arguments, returnsValue] = builtinSignature(instruction);
                if (instruction.argCount != arguments)
                    error(instruction, "incorrect argument count");
                if (instruction.expectsValue != returnsValue)
                    error(instruction, "built-in used in the wrong context");
                instruction.builtin = true;
                continue;
            }
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
                if (tolerateUnresolvedCalls) continue;
                if (!instruction.ownerDefinition.empty()) {
                    const auto owner = definitions_.find(instruction.ownerDefinition);
                    if (owner != definitions_.end() &&
                        std::find(owner->second.parameters.begin(), owner->second.parameters.end(),
                                  instruction.name) != owner->second.parameters.end())
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
            for (const ArrayArgumentCandidate& argument : instruction.arrayArgumentCandidates) {
                const auto array = arrays_.find(argument.name);
                if (array == arrays_.end()) continue;
                std::size_t& parameterRank =
                    definitions_[instruction.name].parameterArrayRanks[argument.parameterIndex];
                if (parameterRank != 0 && parameterRank != array->second)
                    error(instruction, "array parameter is called with inconsistent ranks");
                parameterRank = array->second;
                code_[argument.loadInstruction].op = Op::PushArrayRef;
            }
        }
        bool propagated = true;
        while (propagated) {
            propagated = false;
            for (Instruction& instruction : code_) {
                if (instruction.op != Op::Call || instruction.ownerDefinition.empty()) continue;
                const auto caller = definitions_.find(instruction.ownerDefinition);
                const auto callee = definitions_.find(instruction.name);
                if (caller == definitions_.end() || callee == definitions_.end()) continue;
                for (const ArrayArgumentCandidate& argument : instruction.arrayArgumentCandidates) {
                    const auto parameter = std::find(caller->second.parameters.begin(),
                                                     caller->second.parameters.end(), argument.name);
                    if (parameter == caller->second.parameters.end()) continue;
                    const std::size_t callerIndex = static_cast<std::size_t>(
                        parameter - caller->second.parameters.begin());
                    const std::size_t rank = caller->second.parameterArrayRanks[callerIndex];
                    if (rank == 0) continue;
                    std::size_t& targetRank =
                        definitions_[instruction.name].parameterArrayRanks[argument.parameterIndex];
                    if (targetRank != 0 && targetRank != rank)
                        error(instruction, "array parameter is called with inconsistent ranks");
                    if (targetRank == 0) {
                        targetRank = rank;
                        propagated = true;
                    }
                    code_[argument.loadInstruction].op = Op::PushArrayRef;
                }
            }
        }
        for (const Instruction& instruction : code_) {
            if (instruction.op != Op::Call) continue;
            const auto callee = definitions_.find(instruction.name);
            if (callee == definitions_.end()) continue;
            for (std::size_t i = 0; i < callee->second.parameterArrayRanks.size(); ++i) {
                if (callee->second.parameterArrayRanks[i] == 0) continue;
                const auto argument = std::find_if(instruction.arrayArgumentCandidates.begin(),
                    instruction.arrayArgumentCandidates.end(),
                    [&](const ArrayArgumentCandidate& candidate) {
                        return candidate.parameterIndex == i;
                    });
                if (argument == instruction.arrayArgumentCandidates.end() ||
                    code_[argument->loadInstruction].op != Op::PushArrayRef)
                    error(instruction, "array argument required for array parameter");
            }
        }
        for (Instruction& instruction : code_) {
            if (instruction.op == Op::Call && !instruction.ownerDefinition.empty()) {
                const auto owner = definitions_.find(instruction.ownerDefinition);
                if (owner != definitions_.end()) {
                    const auto parameter = std::find(owner->second.parameters.begin(),
                                                     owner->second.parameters.end(), instruction.name);
                    if (parameter != owner->second.parameters.end()) {
                        const std::size_t index = static_cast<std::size_t>(
                            parameter - owner->second.parameters.begin());
                        const std::size_t rank = owner->second.parameterArrayRanks[index];
                        if (rank == 0) error(instruction, "array parameter has no array argument");
                        if (instruction.argCount != rank)
                            throw std::runtime_error("line " + std::to_string(instruction.line) +
                                ", column " + std::to_string(instruction.column) +
                                ": incorrect array rank (indexes " +
                                std::to_string(instruction.argCount) + ", rank " +
                                std::to_string(rank) + ")");
                        instruction.op = Op::ArrayGet;
                        instruction.rank = rank;
                        continue;
                    }
                }
            }
            if (instruction.op == Op::ArraySet && !instruction.ownerDefinition.empty()) {
                const auto owner = definitions_.find(instruction.ownerDefinition);
                if (owner != definitions_.end()) {
                    const auto parameter = std::find(owner->second.parameters.begin(),
                                                     owner->second.parameters.end(), instruction.name);
                    if (parameter != owner->second.parameters.end()) {
                        const std::size_t index = static_cast<std::size_t>(
                            parameter - owner->second.parameters.begin());
                        instruction.rank = owner->second.parameterArrayRanks[index];
                        if (instruction.rank == 0)
                            error(instruction, "array parameter has no array argument");
                    }
                }
            }
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
    std::size_t callArguments(std::vector<ArrayArgumentCandidate>* arrayCandidates = nullptr) {
        if (!match(Kind::LeftParen)) error(peek(), "expected '(' before arguments");
        std::size_t count = 0;
        if (!check(Kind::RightParen)) {
            do {
                const bool bareName = check(Kind::Identifier) && current_ + 1 < tokens_.size() &&
                    (tokens_[current_ + 1].kind == Kind::Comma ||
                     tokens_[current_ + 1].kind == Kind::RightParen);
                const std::string candidateName = bareName ? peek().text : std::string{};
                const std::size_t expressionStart = code_.size();
                expression();
                if (arrayCandidates && bareName)
                    arrayCandidates->push_back({count, candidateName, expressionStart});
                ++count;
            } while (match(Kind::Comma));
        }
        if (!match(Kind::RightParen)) error(peek(), "expected ')' after arguments");
        return count;
    }
    void emitCall(const Token& name, std::size_t arguments, bool expectsValue,
                  std::vector<ArrayArgumentCandidate> arrayCandidates = {}) {
        Instruction instruction{Op::Call, 0.0, name.text, name.line, name.column};
        instruction.argCount = arguments;
        instruction.expectsValue = expectsValue;
        instruction.arrayArgumentCandidates = std::move(arrayCandidates);
        instruction.ownerDefinition = currentDefinitionName_;
        code_.push_back(std::move(instruction));
    }
    void blockDim() {
        const Token keyword = advance();
        if (!check(Kind::Identifier)) error(peek(), "expected array name after DIM");
        const Token name = advance();
        if (arrays_.find(name.text) != arrays_.end()) error(name, "array is already dimensioned");
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
        std::size_t remainderFalseJump = std::numeric_limits<std::size_t>::max();
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
                emit(Op::JumpIfFalse, armToken);
                remainderFalseJump = code_.size() - 1;
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
        const std::size_t selectEnd = code_.size();
        Instruction finish{Op::SelectEnd, 0.0, {}, keyword.line, keyword.column};
        code_.push_back(std::move(finish));
        if (remainderFalseJump != std::numeric_limits<std::size_t>::max())
            code_[remainderFalseJump].target = selectEnd;
        for (const std::size_t jump : endJumps) code_[jump].target = selectEnd;
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

        if (definitions_.find(name.text) != definitions_.end() || isBuiltinName(name.text))
            error(name, "duplicate or reserved definition name");
        const std::size_t skipIndex = code_.size();
        emit(Op::Jump, defineKeyword);
        DefinitionInfo definition;
        definition.address = code_.size();
        definition.function = isFunction;
        definition.parameters = parameters;
        definition.parameterArrayRanks.resize(parameters.size());
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
        currentDefinitionName_ = name.text;
        block();
        inDefinition_ = false;
        currentDefinitionName_.clear();
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
        if (check(Kind::Identifier) && (arrays_.find(peek().text) != arrays_.end() || isCurrentParameter(peek().text)) &&
            current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == Kind::LeftParen) {
            const Token name = advance();
            const std::size_t rank = callArguments();
            if (arrays_.find(name.text) != arrays_.end() && rank != arrays_.at(name.text))
                error(name, "incorrect array rank");
            if (!match(Kind::Equal)) error(peek(), "expected '=' after array index");
            expression();
            Instruction instruction{Op::ArraySet, 0.0, name.text, name.line, name.column};
            instruction.rank = rank;
            instruction.ownerDefinition = currentDefinitionName_;
            code_.push_back(std::move(instruction));
            return;
        }
        if (check(Kind::Identifier) && current_ + 1 < tokens_.size() &&
            tokens_[current_ + 1].kind == Kind::LeftParen) {
            const Token name = advance();
            std::vector<ArrayArgumentCandidate> arrayCandidates;
            const std::size_t arguments = callArguments(&arrayCandidates);
            emitCall(name, arguments, false, std::move(arrayCandidates));
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
    bool startsStringSlice() const {
        if (!check(Kind::LeftParen)) return false;
        if (current_ + 1 < tokens_.size() &&
            tokens_[current_ + 1].kind == Kind::RightParen) return true;
        std::size_t depth = 0;
        for (std::size_t i = current_ + 1; i < tokens_.size(); ++i) {
            if (tokens_[i].kind == Kind::LeftParen) ++depth;
            else if (tokens_[i].kind == Kind::RightParen) {
                if (depth == 0) return false;
                --depth;
            } else if (tokens_[i].kind == Kind::To && depth == 0) {
                return true;
            } else if (tokens_[i].kind == Kind::End || tokens_[i].kind == Kind::Newline ||
                       tokens_[i].kind == Kind::Colon) {
                return false;
            }
        }
        return false;
    }
    void stringSlice(const Token& name) {
        const Token open = advance();
        emit(Op::Load, name, 0.0, name.text);
        Instruction slice{Op::StringSlice, 0.0, name.text, open.line, open.column};
        if (!check(Kind::To) && !check(Kind::RightParen)) {
            expression();
            slice.sliceHasStart = true;
        }
        if (match(Kind::To)) {
            if (!check(Kind::RightParen)) {
                expression();
                slice.sliceHasEnd = true;
            }
        } else if (slice.sliceHasStart) {
            // A single descriptor selects one character, like SuperBASIC's s$(n).
            slice.sliceSingle = true;
        }
        if (!match(Kind::RightParen)) error(peek(), "expected ')' after string slice");
        code_.push_back(std::move(slice));
    }
    void primary() {
        if (match(Kind::Number)) { const Token t = previous(); emit(Op::Push, t, t.number); return; }
        if (match(Kind::String)) { const Token t = previous(); emit(Op::Push, t, t.text); return; }
        if (match(Kind::Identifier)) {
            const Token t = previous();
            const bool scalarStringSyntax = !isBuiltinName(t.text) && !t.text.empty() &&
                t.text.back() == '$' &&
                arrays_.find(t.text) == arrays_.end() &&
                declaredFunctions_.find(t.text) == declaredFunctions_.end() &&
                check(Kind::LeftParen);
            if (scalarStringSyntax ||
                (!isBuiltinName(t.text) && !t.text.empty() && t.text.back() == '$' &&
                 startsStringSlice())) {
                stringSlice(t);
            } else if (check(Kind::LeftParen)) {
                std::vector<ArrayArgumentCandidate> arrayCandidates;
                const std::size_t arguments = callArguments(&arrayCandidates);
                emitCall(t, arguments, true, std::move(arrayCandidates));
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

std::string joinProgramLines(const std::map<int, std::string>& program) {
    std::string source;
    for (const auto& [number, body] : program) {
        (void)number;
        source += body;
        source.push_back('\n');
    }
    return source;
}

void validateProgramLines(const std::map<int, std::string>& program) {
    Compiler compiler(Lexer(joinProgramLines(program)).scan());
    (void)compiler.compile(true, true);
}

} // namespace

Interpreter::Interpreter(std::shared_ptr<runtime::StorageService> storage,
                         std::shared_ptr<runtime::ChannelService> channels,
                         std::shared_ptr<ui::Service> ui,
                         std::shared_ptr<graphics::Service> graphics)
    : uiSession_(std::make_unique<detail::UISessionState>()),
      storage_(std::move(storage)), channels_(std::move(channels)), ui_(std::move(ui)),
      graphics_(std::move(graphics)),
      session_id_(nextInterpreterSessionId.fetch_add(1, std::memory_order_relaxed)) {
    if (session_id_ == 0)
        session_id_ = nextInterpreterSessionId.fetch_add(1, std::memory_order_relaxed);
}

Interpreter::~Interpreter() {
    if (ui_) ui_->cancelSession(session_id_);
    if (channels_) channels_->closeSession(session_id_);
    if (graphics_) graphics_->closeSession(session_id_);
}

std::vector<detail::Instruction> detail::compileSource(const std::string& source) {
    Compiler compiler(Lexer(source).scan());
    return compiler.compile();
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
    auto parseQuotedPath = [&](const std::string& text, const char* commandName) {
        if (text.empty() || text.front() != '"')
            throw std::runtime_error(std::string(commandName) + " requires a quoted relative path");
        std::string path;
        std::size_t i = 1;
        bool closed = false;
        while (i < text.size()) {
            if (text[i] == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    path.push_back('"');
                    i += 2;
                } else {
                    ++i;
                    closed = true;
                    break;
                }
            } else {
                path.push_back(text[i++]);
            }
        }
        if (!closed || !trim(text.substr(i)).empty())
            throw std::runtime_error("invalid quoted file command path");
        return path;
    };

    bool commandMode = false;
    std::istringstream probe(source);
    for (std::string line; std::getline(probe, line);) {
        const std::size_t numberEnd = lineNumberEnd(line);
        const std::string word = command(line);
        if (numberEnd != 0 || word == "RUN" || word == "LIST" || word == "NEW" ||
            word == "LOAD" || word.rfind("LOAD ", 0) == 0 || word == "SAVE" ||
            word.rfind("SAVE ", 0) == 0 || word == "DIR" || word.rfind("DIR ", 0) == 0) {
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
            std::string body = canonicalizeKeywords(trim(line.substr(numberEnd)));
            std::map<int, std::string> candidate = program_;
            if (body.empty()) candidate.erase(number);
            else candidate[number] = std::move(body);
            validateProgramLines(candidate);
            program_.swap(candidate);
            continue;
        }

        const std::string word = command(line);
        if (word.empty()) continue;
        const std::string normalizedLine = trim(line);
        if (word == "LIST") {
            for (const auto& [number, body] : program_)
                output << number << (body.empty() ? "" : " " + body) << '\n';
            output_column_ = 0;
        } else if (word == "NEW") {
            program_.clear();
            clear();
        } else if (word == "RUN") {
            clear();
            execute(joinProgramLines(program_), output);
        } else if (word == "LOAD" || word.rfind("LOAD ", 0) == 0) {
            if (!storage_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "storage service unavailable");
            const std::string argument = trim(normalizedLine.substr(4));
            const std::string path = parseQuotedPath(argument, "LOAD");
            const std::string contents = storage_->readText(path);
            std::map<int, std::string> candidate;
            std::istringstream file(contents);
            for (std::string fileLine; std::getline(file, fileLine);) {
                const std::string sourceLine = trim(fileLine);
                if (sourceLine.empty()) continue;
                const std::size_t storedNumberEnd = lineNumberEnd(fileLine);
                if (storedNumberEnd == 0)
                    throw std::runtime_error("LOAD expects numbered BASIC source lines");
                int number = 0;
                const auto parsed = std::from_chars(fileLine.data(),
                    fileLine.data() + storedNumberEnd, number);
                if (parsed.ec != std::errc{} || number < 1 || number > 32767)
                    throw std::runtime_error("program line number must be in 1..32767");
                const std::string body = canonicalizeKeywords(
                    trim(fileLine.substr(storedNumberEnd)));
                if (body.empty()) candidate.erase(number);
                else candidate[number] = body;
            }
            validateProgramLines(candidate);
            program_.swap(candidate);
            clear();
        } else if (word == "SAVE" || word.rfind("SAVE ", 0) == 0) {
            if (!storage_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "storage service unavailable");
            const std::string argument = trim(normalizedLine.substr(4));
            const std::string path = parseQuotedPath(argument, "SAVE");
            std::string contents;
            for (const auto& [number, body] : program_)
                contents += std::to_string(number) + (body.empty() ? "" : " " + body) + "\n";
            storage_->writeText(path, contents);
        } else if (word == "DIR" || word.rfind("DIR ", 0) == 0) {
            if (!storage_) throw runtime::ServiceError(runtime::ServiceErrorCode::Unavailable,
                                                        "storage service unavailable");
            std::string path;
            const std::string argument = trim(normalizedLine.substr(3));
            if (!argument.empty()) path = parseQuotedPath(argument, "DIR");
            for (const auto& entry : storage_->list(path))
                output << entry.name << (entry.isDirectory ? "/" : "") << '\n';
            output_column_ = 0;
        } else {
            execute(line + "\n", output);
        }
    }
}

void Interpreter::runRepl(std::istream& input, std::ostream& output,
                          std::ostream& diagnostics, bool showPrompt,
                          std::function<bool()> dispatchUiRequest) {
    std::string line;
    while (true) {
        if (showPrompt) {
            output << "ql32> " << std::flush;
        }
        if (!std::getline(input, line)) break;
        try {
            executeInteractive(line + "\n", output);
        } catch (const std::exception& error) {
            if (!execution_ || !dispatchUiRequest) {
                diagnostics << "ql32: " << error.what() << '\n';
                continue;
            }
            try {
                while (execution_) {
                    if (!dispatchUiRequest())
                        throw std::runtime_error("no host presenter for pending UI request");
                    if (resume(output) == ExecutionStatus::Completed) break;
                }
            } catch (const std::exception& dispatchError) {
                if (ui_) ui_->cancelSession(session_id_);
                execution_.reset();
                diagnostics << "ql32: " << dispatchError.what() << '\n';
            }
        }
    }
}

void Interpreter::clear() {
    if (ui_) ui_->cancelSession(session_id_);
    execution_.reset();
    *uiSession_ = detail::UISessionState{};
    variables_.clear();
    output_column_ = 0;
}

} // namespace ql32
