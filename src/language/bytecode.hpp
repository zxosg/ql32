#pragma once

#include "ql32/interpreter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    Pause, Beep, Volume, MusicPlay, MusicWav, MusicTest, MusicStop, MusicPause, MusicResume, MusicStatus,
    ChannelMirror, ChannelMirrorDelete, ChannelMirrorList
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


// Owns interned identifier names and string literals. Addresses of pooled
// strings stay valid for the pool's lifetime, so instructions hold one pointer.
class TextPool {
public:
    const std::string* intern(const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return &*strings_.insert(value).first;
    }
    std::size_t count() const { return strings_.size(); }

    // Pool that receives text assigned on this thread (set while compiling).
    static TextPool*& active() {
        thread_local TextPool* current = nullptr;
        return current;
    }
    // Fallback for text created outside a compilation (tests, tools).
    static TextPool& fallback() {
        static TextPool pool;
        return pool;
    }

private:
    std::mutex mutex_;
    std::unordered_set<std::string> strings_;
};

class TextPoolScope {
public:
    explicit TextPoolScope(const std::shared_ptr<TextPool>& pool)
        : previous_(TextPool::active()) { TextPool::active() = pool.get(); }
    ~TextPoolScope() { TextPool::active() = previous_; }
    TextPoolScope(const TextPoolScope&) = delete;
    TextPoolScope& operator=(const TextPoolScope&) = delete;

private:
    TextPool* previous_;
};

// Interned immutable text: one pointer into the owning InstructionSequence's
// TextPool (empty text is null).
class SharedText {
public:
    SharedText() = default;
    SharedText(const std::string& value) { assign(value); }
    SharedText(std::string&& value) { assign(value); }
    SharedText(const char* value) { assign(value); }
    SharedText& operator=(const std::string& value) { assign(value); return *this; }
    SharedText& operator=(std::string&& value) { assign(value); return *this; }
    SharedText& operator=(const char* value) { assign(value); return *this; }

    const std::string& str() const noexcept { return text_ ? *text_ : emptyText(); }
    operator const std::string&() const noexcept { return str(); }
    bool empty() const noexcept { return !text_; }
    std::size_t size() const noexcept { return str().size(); }
    char back() const { return str().back(); }
    const char* c_str() const noexcept { return str().c_str(); }
    const void* storageId() const noexcept { return text_; }

private:
    static const std::string& emptyText() {
        static const std::string empty;
        return empty;
    }
    void assign(const std::string& value) {
        if (value.empty()) { text_ = nullptr; return; }
        TextPool* pool = TextPool::active();
        text_ = (pool ? pool : &TextPool::fallback())->intern(value);
    }
    const std::string* text_ = nullptr;
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

// Narrow stored integer that rejects values it cannot hold instead of wrapping.
template <typename T> class Narrow {
public:
    constexpr Narrow() = default;
    Narrow(long long value) { set(value); }
    Narrow& operator=(long long value) { set(value); return *this; }
    constexpr operator int() const noexcept { return value_; }

private:
    void set(long long value) {
        if (value < 0 || value > static_cast<long long>(std::numeric_limits<T>::max()))
            throw std::length_error("program exceeds a bytecode field limit");
        value_ = static_cast<T>(value);
    }
    T value_ = 0;
};

struct InstructionFlags {
    bool explicitNext : 1;
    bool clearTarget : 1;
    bool expectsValue : 1;
    bool builtin : 1;
    bool channelArgument : 1;
    bool sliceHasStart : 1;
    bool sliceHasEnd : 1;
    bool sliceSingle : 1;
    bool sourceJump : 1;
    bool targetIsLineNumber : 1;
    bool statementStart : 1;
    bool textLiteral : 1;
    InstructionFlags() : explicitNext(false), clearTarget(false), expectsValue(false), builtin(false), channelArgument(false), sliceHasStart(false), sliceHasEnd(false), sliceSingle(false), sourceJump(false), targetIsLineNumber(false), statementStart(false), textLiteral(false) {}
};

// Pointers first so the flag bits share the tail of the first cache line
// instead of forcing alignment padding.
struct InstructionHead {
    SharedText name;
    ExtrasPtr extras;
};

struct Instruction : InstructionHead, InstructionFlags {
    Instruction() = default;
    // `literal` is only meaningful for Op::Push; a string literal goes in `name`.
    Instruction(Op opcode, double literal = 0.0, SharedText text = {}, long long sourceLine = 1,
                long long sourceColumn = 1, PrintSeparator sep = PrintSeparator::None)
        : op(opcode), separator(sep) {
        name = std::move(text);
        line = sourceLine;
        column = sourceColumn;
        if (opcode == Op::Push) setLiteral(literal);
    }

    // Numeric literal of Op::Push, held in the jump-target words Push never uses.
    double literal() const noexcept {
        std::uint64_t bits = (static_cast<std::uint64_t>(exitTarget) << 32) | target;
        double result;
        std::memcpy(&result, &bits, sizeof result);
        return result;
    }
    void setLiteral(double value) noexcept {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        target = static_cast<std::uint32_t>(bits);
        exitTarget = static_cast<std::uint32_t>(bits >> 32);
    }

    // Read access never allocates; mutableExtra() creates the side record.
    const InstructionExtras& extra() const { return extras.get(); }
    InstructionExtras& mutableExtra() { return extras.mutate(); }

    // Declaration order is deliberate: the 16-bit fields fill the tail padding
    // after the flag bits before the 32-bit words start.
    Narrow<std::uint16_t> line = 1;
    Narrow<std::uint16_t> column = 1;
    Narrow<std::uint16_t> basicLine = 0;
    std::uint32_t target = 0;
    std::uint32_t exitTarget = 0;
    // One operand word shared by opcodes that never overlap:
    //   ForInit/ForNext/LoopNext/ExitLoop: loopId (all 32 bits)
    //   Call, Beep and array ops:          argCount (low 16), optionCount (high 16)
    //   Goto/Gosub/Restore by number:      sourceLineTarget (low 16)
    std::uint32_t loopId() const noexcept { return operand_; }
    void setLoopId(std::size_t value) {
        if (value > 0xFFFFFFFFull) throw std::length_error("program exceeds a bytecode field limit");
        operand_ = static_cast<std::uint32_t>(value);
    }
    int argCount() const noexcept { return static_cast<int>(operand_ & 0xFFFFu); }
    void setArgCount(long long value) { operand_ = (operand_ & 0xFFFF0000u) | checked16(value); }
    int optionCount() const noexcept { return static_cast<int>(operand_ >> 16); }
    void setOptionCount(long long value) { operand_ = (operand_ & 0xFFFFu) | (checked16(value) << 16); }
    int sourceLineTarget() const noexcept { return argCount(); }
    void setSourceLineTarget(long long value) { setArgCount(value); }

    std::uint32_t operand_ = 0;
    Narrow<std::uint8_t> rank = 0;
    Op op = Op::Push;
    PrintSeparator separator = PrintSeparator::None;

private:
    static std::uint32_t checked16(long long value) {
        if (value < 0 || value > 0xFFFF) throw std::length_error("program exceeds a bytecode field limit");
        return static_cast<std::uint32_t>(value);
    }
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
    std::shared_ptr<TextPool> textPool = std::make_shared<TextPool>();
};

InstructionSequence compileSource(const std::string& source);
InstructionSequence compileExpression(const std::string& source,
                                      const InstructionSequence& enclosing);
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
