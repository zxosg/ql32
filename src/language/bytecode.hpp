#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
    std::size_t capacity() const noexcept { return values_ ? values_->capacity() : 0; }
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
    std::size_t capacity() const noexcept { return value_ ? value_->capacity() : 0; }
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
    static constexpr std::uint32_t NoSymbolSlot = UINT32_MAX;
    std::uint32_t symbolSlot = NoSymbolSlot;
    std::uint32_t localSlot = NoSymbolSlot;
    std::size_t rank = 0;
    bool sliceHasStart = false;
    bool sliceHasEnd = false;
    bool sliceSingle = false;
    OptionalVector<SelectItemSpec> selectItems;
    bool sourceJump = false;
    bool targetIsLineNumber = false;
    bool statementStart = false;
    int basicLine = 0;
    int sourceLineTarget = 0;
    OptionalVector<std::size_t> jumpScopes;
};

// Compact common-case record. Instructions with names, control-flow metadata,
// or other uncommon payloads live in CompactProgram's indexed side table.
struct CompactInstruction {
    std::uint8_t opcode = 0;
    std::uint8_t flags = 0;
    std::uint16_t line = 1;
    std::uint16_t column = 1;
    std::uint16_t basicLine = 0;
    union Payload {
        double number;
        std::uint64_t sidecarIndex;
        constexpr Payload() : number(0.0) {}
    } payload;
};
static_assert(sizeof(CompactInstruction) == 16,
              "compact bytecode records must remain 16 bytes");
static_assert(static_cast<unsigned>(Op::SelectEnd) <= UINT8_MAX,
              "compact bytecode opcode field is too small");

struct FunctionMetadata {
    std::size_t address = 0;
    bool function = false;
    std::vector<std::string> parameters;
    std::vector<std::string> locals;
    std::vector<std::string> localSymbols;
};

struct ProgramMetadata {
    NumericPrecision numericPrecision = NumericPrecision::Double;
    std::uint16_t cpuFrequencyMHz = 0;
    std::unordered_map<std::string, FunctionMetadata> functions;
    std::unordered_map<std::string, std::size_t> arrays;
    std::unordered_map<std::string, Value> constants;
};

// Compiler-only rich instruction builder. VM execution lowers this transient
// form into fixed-size records plus indexed side tables below.
struct InstructionSequence : std::deque<Instruction>, ProgramMetadata {};

class CompactProgram : public ProgramMetadata {
public:
    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Instruction;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = Instruction;

        Instruction operator*() const;
        const_iterator& operator++() { ++index_; return *this; }
        const_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) {
            return a.program_ == b.program_ && a.index_ == b.index_;
        }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) {
            return !(a == b);
        }

    private:
        friend class CompactProgram;
        const_iterator(const CompactProgram* program, std::size_t index)
            : program_(program), index_(index) {}
        const CompactProgram* program_ = nullptr;
        std::size_t index_ = 0;
    };

    CompactProgram() = default;
    explicit CompactProgram(InstructionSequence&& rich);
    std::size_t size() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    bool isSidecar(std::size_t index) const {
        if (index >= records_.size()) throw std::out_of_range("bytecode address out of range");
        return records_[index].flags == UINT8_MAX;
    }
    Instruction operator[](std::size_t index) const;
    Instruction at(std::size_t index) const;
    const Instruction& instructionAt(std::size_t index, Instruction& scratch) const;
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }
    const std::vector<CompactInstruction>& encodedRecords() const noexcept { return records_; }
    const std::vector<Instruction>& encodedSidecars() const noexcept { return sidecars_; }
    const std::vector<std::string>& encodedStringLiterals() const noexcept {
        return stringLiterals_;
    }
    void appendRange(const InstructionSequence& instructions);
    void appendRange(const CompactProgram& instructions);
    void push_back(Instruction instruction) { appendOne(std::move(instruction)); }
    template <typename Function>
    void updateAt(std::size_t index, Function&& function) {
        updateAtImpl(index, std::function<void(Instruction&)>(
            std::forward<Function>(function)));
    }
    void eraseFrom(std::size_t first);
    template <typename Function>
    void forEachSidecar(Function&& function) {
        for (Instruction& instruction : sidecars_) function(instruction);
    }
    std::size_t compactRecordCount() const noexcept { return compactRecordCount_; }
    std::size_t sidecarCount() const noexcept { return sidecars_.size(); }
    std::size_t encodedStorageBytes() const noexcept;

private:
    const Instruction& decode(std::size_t index, Instruction& scratch) const;
    void updateAtImpl(std::size_t index,
                      const std::function<void(Instruction&)>& function);
    void appendOne(Instruction instruction);
    std::vector<CompactInstruction> records_;
    std::vector<Instruction> sidecars_;
    std::vector<std::string> stringLiterals_;
    std::size_t compactRecordCount_ = 0;
};

// Compiler storage that can keep either the legacy rich sequence or emit into
// compact records as instructions are produced. Metadata is shared by both
// modes; mutable compact instructions are promoted to sidecars on demand.
#ifdef QL32_TEST_LEGACY_COMPILER
void compilerEmissionCheckpointForTest();
#endif
class CompilerCode : public ProgramMetadata {
public:
    explicit CompilerCode(bool compactEmission = false)
        : compactEmission_(compactEmission) {}

    std::size_t size() const noexcept {
        return compactEmission_ ? compact_.size() : rich_.size();
    }
    void push_back(Instruction instruction) {
#ifdef QL32_TEST_LEGACY_COMPILER
        if (compactEmission_) compilerEmissionCheckpointForTest();
#endif
        if (compactEmission_) compact_.push_back(std::move(instruction));
        else rich_.push_back(std::move(instruction));
    }
    Instruction readAt(std::size_t index) const {
        return compactEmission_ ? compact_.at(index) : rich_.at(index);
    }
    template <typename Function>
    void updateAt(std::size_t index, Function&& function) {
        if (compactEmission_) compact_.updateAt(index, std::forward<Function>(function));
        else function(rich_.at(index));
    }
    template <typename Function>
    void forEachMutable(Function&& function) {
        if (!compactEmission_) {
            for (Instruction& instruction : rich_) function(instruction);
            return;
        }
        Instruction scratch;
        for (std::size_t index = 0; index < compact_.size(); ++index) {
            if (!compact_.isSidecar(index)) continue;
            Instruction instruction = compact_.instructionAt(index, scratch);
            function(instruction);
            compact_.updateAt(index, [&](Instruction& current) {
                current = std::move(instruction);
            });
        }
    }
    InstructionSequence takeRich() {
        InstructionSequence result;
        static_cast<std::deque<Instruction>&>(result) = std::move(rich_);
        static_cast<ProgramMetadata&>(result) = std::move(
            static_cast<ProgramMetadata&>(*this));
        return result;
    }
    CompactProgram takeCompact() {
        if (!compactEmission_) {
            CompactProgram result;
            for (Instruction& instruction : rich_)
                result.push_back(std::move(instruction));
            rich_.clear();
            static_cast<ProgramMetadata&>(result) = std::move(
                static_cast<ProgramMetadata&>(*this));
            return result;
        }
        static_cast<ProgramMetadata&>(compact_) = std::move(
            static_cast<ProgramMetadata&>(*this));
        return std::move(compact_);
    }

private:
    bool compactEmission_ = false;
    std::deque<Instruction> rich_;
    CompactProgram compact_;
};

CompactProgram compileSourceCompact(const std::string& source);
#ifdef QL32_TEST_LEGACY_COMPILER
InstructionSequence compileSource(const std::string& source);
InstructionSequence compileExpression(const std::string& source,
                                      const ProgramMetadata& enclosing);
void failCompactEmissionAfterForTest(std::size_t instructionCount);
#endif
CompactProgram compileExpressionCompact(const std::string& source,
                                        const ProgramMetadata& enclosing);
// Counts container objects, element storage and string capacities owned by the
// sequence. Allocator bookkeeping and deque block slack are implementation-specific.
std::size_t estimateInstructionSequenceBytes(const InstructionSequence& code) noexcept;
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
