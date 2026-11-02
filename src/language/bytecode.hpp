#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ql32::detail {

enum class PrintSeparator { None, Semicolon, Exclamation, Comma, Backslash };
enum class NumericPrecision { Double, Single };
enum class Op {
    Push, Load, Store, Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
    Power, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    And, Or, Xor, Negate, Not, PrintBegin, PrintChannel, Print, PrintSeparator, PrintEnd,
    ChannelDefault, ChannelRoute, ChannelSeek, ChannelRead, ChannelWrite, Input, JumpIfFalse, Jump,
    ForInit, ForNext, LoopNext, ExitLoop, Call, Return, FunctionEnd,
    DimArray, ArrayGet, ArraySet, StringSlice, PushArrayRef, SelectBegin, SelectMatch, SelectRemainder, SelectEnd
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
    OptionalVector<ForItemSpec> forItems;
    bool explicitNext = false;
    bool clearTarget = false;
    std::size_t argCount = 0;
    bool expectsValue = false;
    bool builtin = false;
    bool channelArgument = false;
    OptionalVector<std::string> parameters;
    OptionalVector<std::string> localNames;
    OptionalVector<ArrayArgumentCandidate> arrayArgumentCandidates;
    OptionalString ownerDefinition;
    OptionalString targetVariable;
    std::size_t rank = 0;
    bool sliceHasStart = false;
    bool sliceHasEnd = false;
    bool sliceSingle = false;
    OptionalVector<SelectItemSpec> selectItems;
    bool sourceJump = false;
    bool targetIsLineNumber = false;
    int sourceLineTarget = 0;
    OptionalVector<std::size_t> jumpScopes;
};

// Chunked instruction storage keeps bytecode growth from requiring one large
// contiguous allocation on memory-constrained targets.
struct InstructionSequence : std::deque<Instruction> {
    NumericPrecision numericPrecision = NumericPrecision::Double;
    std::uint16_t cpuFrequencyMHz = 0;
};

InstructionSequence compileSource(const std::string& source);
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
