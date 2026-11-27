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
#include <string_view>
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
    ChannelMirror, ChannelMirrorDelete, ChannelMirrorList, TrapBegin, TrapEnd, Raise
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

// Parameter/local name lists are identical for every call of a procedure:
// calls share one immutable list and copy it only when modified.
class SharedNames {
public:
    using Vector = std::vector<std::string>;
    using const_iterator = Vector::const_iterator;
    using iterator = const_iterator;
    SharedNames() = default;
    explicit SharedNames(std::shared_ptr<const Vector> shared) : values_(std::move(shared)) {}
    SharedNames& operator=(const Vector& values) {
        if (values.empty()) values_.reset();
        else values_ = std::make_shared<const Vector>(values);
        return *this;
    }
    SharedNames& operator=(Vector&& values) {
        if (values.empty()) values_.reset();
        else values_ = std::make_shared<const Vector>(std::move(values));
        return *this;
    }
    bool empty() const noexcept { return !values_ || values_->empty(); }
    std::size_t size() const noexcept { return values_ ? values_->size() : 0; }
    const_iterator begin() const { return values_ ? values_->begin() : emptyVector().begin(); }
    const_iterator end() const { return values_ ? values_->end() : emptyVector().end(); }
    const std::string& operator[](std::size_t index) const { return (*values_)[index]; }
    void push_back(std::string value) {
        Vector copy = values_ ? *values_ : Vector{};
        copy.push_back(std::move(value));
        values_ = std::make_shared<const Vector>(std::move(copy));
    }

private:
    static const Vector& emptyVector() {
        static const Vector empty;
        return empty;
    }
    std::shared_ptr<const Vector> values_;
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
        if (frozen_) rebuildIndex();
        const auto found = index_.find(std::string_view(value));
        if (found != index_.end()) return &store_[found->second];
        store_.push_back(value);
        index_.emplace(std::string_view(store_.back()), store_.size() - 1);
        return &store_.back();
    }
    std::size_t count() const { return store_.size(); }
    // Drops the lookup index once compilation is done; a later intern() (EVAL) rebuilds it.
    void freeze() {
        std::lock_guard<std::mutex> lock(mutex_);
        index_ = {};
        frozen_ = true;
    }

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
    void rebuildIndex() {
        index_.reserve(store_.size());
        for (std::size_t i = 0; i < store_.size(); ++i) index_.emplace(std::string_view(store_[i]), i);
        frozen_ = false;
    }
    std::mutex mutex_;
    std::deque<std::string> store_;  // stable addresses; SharedText points into it
    std::unordered_map<std::string_view, std::size_t> index_;
    bool frozen_ = false;
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
    const std::string& get() const noexcept { return str(); }
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
    SharedNames parameters;
    OptionalVector<std::string> inputVariables;
    SharedNames localNames;
    OptionalVector<ArrayArgumentCandidate> arrayArgumentCandidates;
    SharedText ownerDefinition;
    OptionalString targetVariable;
    OptionalVector<SelectItemSpec> selectItems;
    OptionalVector<std::size_t> jumpScopes;

    // ownerDefinition, jumpScopes and arrayArgumentCandidates are only read while
    // the program is being compiled and resolved.
    void dropCompileOnly() {
        arrayArgumentCandidates = std::vector<ArrayArgumentCandidate>{};
        ownerDefinition = std::string{};
        jumpScopes = std::vector<std::size_t>{};
    }
    bool hasRuntimeData() const {
        return !forItems.empty() || !parameters.empty() || !inputVariables.empty() ||
               !localNames.empty() || !targetVariable.empty() || !selectItems.empty();
    }
};

// Owns a compile-time extras record, or borrows one that lives in a compact
// program's table (decoded instructions never copy their extras). The low bit
// of the pointer marks a borrowed record.
class ExtrasPtr {
public:
    ExtrasPtr() = default;
    ExtrasPtr(const ExtrasPtr& other) { copyFrom(other); }
    ExtrasPtr& operator=(const ExtrasPtr& other) {
        if (this != &other) { release(); copyFrom(other); }
        return *this;
    }
    ExtrasPtr(ExtrasPtr&& other) noexcept : bits_(other.bits_) { other.bits_ = 0; }
    ExtrasPtr& operator=(ExtrasPtr&& other) noexcept {
        if (this != &other) { release(); bits_ = other.bits_; other.bits_ = 0; }
        return *this;
    }
    ~ExtrasPtr() { release(); }
    const InstructionExtras& get() const {
        static const InstructionExtras none;
        const InstructionExtras* value = pointer();
        return value ? *value : none;
    }
    InstructionExtras& mutate() {
        if (bits_ == 0) bits_ = reinterpret_cast<std::uintptr_t>(new InstructionExtras());
        else if (bits_ & 1u) bits_ = reinterpret_cast<std::uintptr_t>(new InstructionExtras(*pointer()));
        return *pointer();
    }
    // Points at a record owned elsewhere; it must outlive this instruction.
    void borrow(const InstructionExtras* record) {
        release();
        bits_ = record ? (reinterpret_cast<std::uintptr_t>(record) | 1u) : 0;
    }
    void clear() { release(); }
    bool present() const noexcept { return bits_ != 0; }
    // Frees the record once nothing the VM reads remains in it.
    void dropCompileOnly() {
        if (bits_ == 0 || (bits_ & 1u)) return;
        InstructionExtras* value = pointer();
        value->dropCompileOnly();
        if (!value->hasRuntimeData()) release();
    }

private:
    InstructionExtras* pointer() const {
        return reinterpret_cast<InstructionExtras*>(bits_ & ~std::uintptr_t{1});
    }
    void release() {
        if (bits_ != 0 && !(bits_ & 1u)) delete pointer();
        bits_ = 0;
    }
    void copyFrom(const ExtrasPtr& other) {
        if (other.bits_ & 1u) bits_ = other.bits_;
        else bits_ = other.bits_ ? reinterpret_cast<std::uintptr_t>(new InstructionExtras(*other.pointer())) : 0;
    }
    std::uintptr_t bits_ = 0;
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
    void dropCompileOnlyExtras() { extras.dropCompileOnly(); }

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

// Out-of-line holder for the compact runtime form (see compact_program.hpp);
// deep-copies so InstructionSequence stays a value type.
class CompactState;
class CompactHolder {
public:
    CompactHolder();
    CompactHolder(const CompactHolder& other);
    CompactHolder& operator=(const CompactHolder& other);
    CompactHolder(CompactHolder&&) noexcept;
    CompactHolder& operator=(CompactHolder&&) noexcept;
    ~CompactHolder();
    CompactState* get() const noexcept { return state_.get(); }
    void reset() noexcept;
    CompactState& make();

private:
    std::unique_ptr<CompactState> state_;
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
    CompactHolder compact;

    // Runtime access. Once makeCompact() ran, the deque is empty (unless the
    // dual-run debug mode keeps it) and these read the compact program; the
    // reference stays valid for the next few lookups only.
    std::size_t instructionCount() const;
    const Instruction& instructionAt(std::size_t index) const;
    void truncateInstructions(std::size_t count);
    void appendInstruction(Instruction instruction);
    void appendInstructions(const InstructionSequence& other);
    // Replaces the instruction deque with the compact encoding.
    void makeCompact();
    bool isCompact() const noexcept { return compact.get() != nullptr; }
};

// When `memory` is given, a program whose token list cannot fit the free heap
// is refused with a clear message instead of failing inside the allocator.
InstructionSequence compileSource(const std::string& source,
                                  const runtime::MemoryService* memory = nullptr);
InstructionSequence compileExpression(const std::string& source,
                                      const InstructionSequence& enclosing);
std::string expandIncludes(const std::string& source, runtime::StorageService* storage);

} // namespace ql32::detail
