#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ql32::detail {

enum class PrintSeparator : std::uint8_t { None, Semicolon, Exclamation, Comma, Backslash };
enum class NumericPrecision { Double, Single };
enum class Op : std::uint8_t {
    Push, Load, Store, Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
    Power, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Negate, Not, PrintBegin, PrintChannel, PrintStyle, Print, PrintSeparator, PrintEnd, PrintCommit,
    ChannelDefault, ChannelRoute, ChannelSeek, ChannelRead, ChannelWrite, At, Read, Restore, Stop,
    Input, JumpIfFalse, Jump,
    ForInit, ForNext, LoopNext, ExitLoop, Call, Return, FunctionEnd,
    DimArray, ArrayGet, ArraySet, StringSlice, PushArrayRef, SelectBegin, SelectMatch, SelectRemainder, SelectEnd,
    Pause, Beep, Volume, MusicPlay, MusicWav, MusicTest, MusicStop, MusicPause, MusicResume, MusicStatus
};

struct ForItemSpec {
    bool hasEnd = false;
    bool hasStep = false;
};

struct SelectItemSpec {
    bool hasEnd = false;
};


struct ArrayArgumentCandidate {
    std::size_t parameterIndex = 0;
    std::string name;
    std::size_t loadInstruction = 0;
};

// Most opcodes need none of their optional metadata. Keep empty sequences to
// one pointer per instruction and allocate the vector only when used.
template <typename T>
class OptionalVector {
public:
    using Vector = std::vector<T>;
    using iterator = typename Vector::iterator;
    using const_iterator = typename Vector::const_iterator;

    OptionalVector() = default;
    OptionalVector(const OptionalVector& other) {
        if (other.values_) values_ = std::make_unique<Vector>(*other.values_);
    }
    OptionalVector& operator=(const OptionalVector& other) {
        if (this == &other) return *this;
        if (other.values_) values_ = std::make_unique<Vector>(*other.values_);
        else values_.reset();
        return *this;
    }
    OptionalVector(OptionalVector&&) noexcept = default;
    OptionalVector& operator=(OptionalVector&&) noexcept = default;
    OptionalVector& operator=(const Vector& values) {
        if (values.empty()) values_.reset();
        else values_ = std::make_unique<Vector>(values);
        return *this;
    }
    OptionalVector& operator=(Vector&& values) {
        if (values.empty()) values_.reset();
        else values_ = std::make_unique<Vector>(std::move(values));
        return *this;
    }

    bool empty() const noexcept { return !values_ || values_->empty(); }
    std::size_t size() const noexcept { return values_ ? values_->size() : 0; }
    iterator begin() { return values_ ? values_->begin() : emptyVector().begin(); }
    iterator end() { return values_ ? values_->end() : emptyVector().end(); }
    const_iterator begin() const { return values_ ? values_->begin() : emptyVector().begin(); }
    const_iterator end() const { return values_ ? values_->end() : emptyVector().end(); }
    T& operator[](std::size_t index) { return (*values_)[index]; }
    const T& operator[](std::size_t index) const { return (*values_)[index]; }
    void push_back(const T& value) { ensure().push_back(value); }
    void push_back(T&& value) { ensure().push_back(std::move(value)); }
    void resize(std::size_t size) {
        if (size == 0) values_.reset();
        else ensure().resize(size);
    }

private:
    static Vector& emptyVector() {
        static Vector empty;
        return empty;
    }
    Vector& ensure() {
        if (!values_) values_ = std::make_unique<Vector>();
        return *values_;
    }
    std::unique_ptr<Vector> values_;
};

// These two labels are empty on almost every instruction. Store nonempty
// values out of line to keep the common bytecode record compact.
class OptionalString {
public:
    OptionalString() = default;
    OptionalString(const OptionalString& other) {
        if (other.value_) value_ = std::make_unique<std::string>(*other.value_);
    }
    OptionalString& operator=(const OptionalString& other) {
        if (this == &other) return *this;
        if (other.value_) value_ = std::make_unique<std::string>(*other.value_);
        else value_.reset();
        return *this;
    }
    OptionalString(OptionalString&&) noexcept = default;
    OptionalString& operator=(OptionalString&&) noexcept = default;
    OptionalString& operator=(const std::string& value) {
        if (value.empty()) value_.reset();
        else value_ = std::make_unique<std::string>(value);
        return *this;
    }
    OptionalString& operator=(std::string&& value) {
        if (value.empty()) value_.reset();
        else value_ = std::make_unique<std::string>(std::move(value));
        return *this;
    }
    bool empty() const noexcept { return !value_ || value_->empty(); }
    char back() const { return get().back(); }
    const std::string& get() const {
        static const std::string emptyValue;
        return value_ ? *value_ : emptyValue;
    }
    bool operator==(const OptionalString& other) const { return get() == other.get(); }
    bool operator!=(const OptionalString& other) const { return !(*this == other); }

private:
    std::unique_ptr<std::string> value_;
};


// Shared immutable text. Identifier names and string literals repeat across
// instructions; compileSource() collapses equal values onto one allocation.
class SharedText {
public:
    SharedText() = default;
    SharedText(const std::string& value) { assign(value); }
    SharedText(std::string&& value) { assign(std::move(value)); }
    SharedText(const char* value) { assign(value); }
    SharedText& operator=(const std::string& value) { assign(value); return *this; }
    SharedText& operator=(std::string&& value) { assign(std::move(value)); return *this; }
    SharedText& operator=(const char* value) { assign(value); return *this; }

    const std::string& str() const noexcept { return text_ ? *text_ : emptyText(); }
    operator const std::string&() const noexcept { return str(); }
    bool empty() const noexcept { return !text_; }
    std::size_t size() const noexcept { return str().size(); }
    char back() const { return str().back(); }
    const char* c_str() const noexcept { return str().c_str(); }
    // Replace this text with the canonical allocation for an equal string.
    void intern(std::unordered_map<std::string, std::shared_ptr<const std::string>>& pool) {
        if (!text_) return;
        auto found = pool.find(*text_);
        if (found == pool.end()) found = pool.emplace(*text_, text_).first;
        text_ = found->second;
    }
    const void* storageId() const noexcept { return text_.get(); }

private:
    static const std::string& emptyText() {
        static const std::string empty;
        return empty;
    }
    template <typename T> void assign(T&& value) {
        if (std::string(value).empty()) text_.reset();
        else text_ = std::make_shared<const std::string>(std::forward<T>(value));
    }
    std::shared_ptr<const std::string> text_;
};
inline bool operator==(const SharedText& a, const SharedText& b) { return a.str() == b.str(); }
inline bool operator!=(const SharedText& a, const SharedText& b) { return a.str() != b.str(); }
inline bool operator==(const SharedText& a, const std::string& b) { return a.str() == b; }
inline bool operator!=(const SharedText& a, const std::string& b) { return a.str() != b; }
inline bool operator==(const std::string& a, const SharedText& b) { return a == b.str(); }
inline bool operator!=(const std::string& a, const SharedText& b) { return a != b.str(); }
inline bool operator==(const SharedText& a, const char* b) { return a.str() == b; }
inline bool operator!=(const SharedText& a, const char* b) { return a.str() != b; }
inline bool operator<(const SharedText& a, const SharedText& b) { return a.str() < b.str(); }
inline std::string operator+(const std::string& a, const SharedText& b) { return a + b.str(); }
inline std::string operator+(const SharedText& a, const std::string& b) { return a.str() + b; }
inline std::string operator+(const char* a, const SharedText& b) { return a + b.str(); }
inline std::string operator+(const SharedText& a, const char* b) { return a.str() + b; }

// Metadata only a few opcodes (FOR, SELECT, CALL, INPUT, source jumps, ...)
// need. Instructions without any of it carry a single null pointer.
struct InstructionExtras {
    OptionalVector<ForItemSpec> forItems;
    OptionalVector<std::string> parameters;
    OptionalVector<std::string> inputVariables;
    OptionalVector<std::string> localNames;
    OptionalVector<ArrayArgumentCandidate> arrayArgumentCandidates;
    OptionalString ownerDefinition;
    OptionalString targetVariable;
    OptionalVector<SelectItemSpec> selectItems;
    OptionalVector<std::size_t> jumpScopes;
};

class ExtrasPtr {
public:
    ExtrasPtr() = default;
    ExtrasPtr(const ExtrasPtr& other) {
        if (other.value_) value_ = std::make_unique<InstructionExtras>(*other.value_);
    }
    ExtrasPtr& operator=(const ExtrasPtr& other) {
        if (this == &other) return *this;
        if (other.value_) value_ = std::make_unique<InstructionExtras>(*other.value_);
        else value_.reset();
        return *this;
    }
    ExtrasPtr(ExtrasPtr&&) noexcept = default;
    ExtrasPtr& operator=(ExtrasPtr&&) noexcept = default;
    const InstructionExtras& get() const {
        static const InstructionExtras none;
        return value_ ? *value_ : none;
    }
    InstructionExtras& mutate() {
        if (!value_) value_ = std::make_unique<InstructionExtras>();
        return *value_;
    }

private:
    std::unique_ptr<InstructionExtras> value_;
};

struct Instruction {
    Op op;
    // Numeric literal (Op::Push). A string literal is stored in `name` and
    // flagged by textLiteral, so no instruction carries a second text pointer.
    double value = 0.0;
    SharedText name;
    int line = 1;
    int column = 1;
    PrintSeparator separator = PrintSeparator::None;
    std::uint32_t target = 0;
    std::uint32_t exitTarget = 0;
    std::uint32_t loopId = 0;
    bool explicitNext = false;
    bool clearTarget = false;
    std::uint32_t argCount = 0;
    std::uint32_t optionCount = 0;
    bool expectsValue = false;
    bool builtin = false;
    bool channelArgument = false;
    std::uint32_t rank = 0;
    bool sliceHasStart = false;
    bool sliceHasEnd = false;
    bool sliceSingle = false;
    bool sourceJump = false;
    bool targetIsLineNumber = false;
    bool statementStart = false;
    bool textLiteral = false;
    int basicLine = 0;
    int sourceLineTarget = 0;
    ExtrasPtr extras;

    // Read access never allocates; mutableExtra() creates the side record.
    const InstructionExtras& extra() const { return extras.get(); }
    InstructionExtras& mutableExtra() { return extras.mutate(); }
};

struct FunctionMetadata {
    std::size_t address = 0;
    bool function = false;
    std::vector<std::string> parameters;
    std::vector<std::string> locals;
};

struct DataItem {
    std::size_t address = 0;
    int basicLine = 0;
};

// Chunked instruction storage keeps bytecode growth from requiring one large
// contiguous allocation on memory-constrained targets.
struct InstructionSequence : std::deque<Instruction> {
    NumericPrecision numericPrecision = NumericPrecision::Double;
    std::uint16_t cpuFrequencyMHz = 0;
    std::unordered_map<std::string, FunctionMetadata> functions;
    std::unordered_map<std::string, std::size_t> arrays;
    std::unordered_map<std::string, Value> constants;
    std::vector<DataItem> dataItems;
};

InstructionSequence compileSource(const std::string& source);
InstructionSequence compileExpression(const std::string& source,
                                      const InstructionSequence& enclosing);
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
