#include "ql32/music/lh5.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace ql32::music {
namespace {
constexpr unsigned DictionaryBits = 13;
constexpr unsigned DictionarySize = 1u << DictionaryBits;
constexpr unsigned MaxMatch = 256;
constexpr unsigned Threshold = 3;
constexpr unsigned Nc = 256 + MaxMatch - Threshold + 1;
constexpr unsigned Nt = 19;
constexpr unsigned Np = DictionaryBits + 1;
constexpr unsigned CBits = 9;
constexpr unsigned TBits = 5;
constexpr unsigned PBits = 4;
constexpr unsigned MaxCodeBits = 16;

class Bits {
public:
    explicit Bits(const Lh5ByteReader& reader) : reader_(reader), buffer_(256) {}

    bool get(unsigned count, unsigned& result) {
        result = 0;
        if (count > 24) return false;
        for (unsigned i = 0; i < count; ++i) {
            if (available_ == 0 && !refill()) return false;
            result = (result << 1) | ((buffer_[position_] >> (7 - bit_)) & 1u);
            if (++bit_ == 8) {
                bit_ = 0;
                ++position_;
                --available_;
            }
        }
        return true;
    }

private:
    bool refill() {
        position_ = 0;
        available_ = reader_(buffer_.data(), buffer_.size());
        return available_ != 0;
    }

    const Lh5ByteReader& reader_;
    std::vector<std::uint8_t> buffer_;
    std::size_t position_ = 0;
    std::size_t available_ = 0;
    unsigned bit_ = 0;
};

struct Huffman {
    std::array<unsigned, MaxCodeBits + 1> count{};
    std::array<unsigned, MaxCodeBits + 1> first{};
    std::array<unsigned, MaxCodeBits + 1> index{};
    std::vector<unsigned> symbols;
    unsigned constant = 0;
    bool isConstant = false;

    bool build(const std::vector<unsigned>& lengths) {
        count.fill(0);
        first.fill(0);
        index.fill(0);
        symbols.clear();
        isConstant = false;
        for (unsigned length : lengths) {
            if (length > MaxCodeBits) return false;
            if (length != 0) ++count[length];
        }
        unsigned code = 0;
        unsigned total = 0;
        for (unsigned length = 1; length <= MaxCodeBits; ++length) {
            code = (code + count[length - 1]) << 1;
            first[length] = code;
            index[length] = total;
            total += count[length];
        }
        symbols.reserve(total);
        for (unsigned length = 1; length <= MaxCodeBits; ++length)
            for (unsigned symbol = 0; symbol < lengths.size(); ++symbol)
                if (lengths[symbol] == length) symbols.push_back(symbol);
        return total != 0;
    }

    bool decode(Bits& bits, unsigned& symbol) const {
        if (isConstant) {
            symbol = constant;
            return true;
        }
        unsigned code = 0;
        for (unsigned length = 1; length <= MaxCodeBits; ++length) {
            unsigned bit;
            if (!bits.get(1, bit)) return false;
            code = (code << 1) | bit;
            const unsigned offset = code - first[length];
            if (code >= first[length] && offset < count[length]) {
                const unsigned at = index[length] + offset;
                if (at >= symbols.size()) return false;
                symbol = symbols[at];
                return true;
            }
        }
        return false;
    }
};

bool readPtLengths(Bits& bits, unsigned count, unsigned countBits,
                   int special, Huffman& tree) {
    unsigned used;
    if (!bits.get(countBits, used) || used > count) return false;
    std::vector<unsigned> lengths(count, 0);
    if (used == 0) {
        unsigned symbol;
        if (!bits.get(countBits, symbol) || symbol >= count) return false;
        tree = {};
        tree.isConstant = true;
        tree.constant = symbol;
        return true;
    }
    unsigned i = 0;
    while (i < used) {
        unsigned length;
        if (!bits.get(3, length)) return false;
        if (length == 7) {
            unsigned bit;
            do {
                if (!bits.get(1, bit)) return false;
                if (bit) ++length;
            } while (bit && length <= MaxCodeBits);
        }
        if (length > MaxCodeBits) return false;
        lengths[i++] = length;
        // LHA inserts the special zero run after the entry at i_special - 1.
        // The reference decoder tests the cursor after incrementing it.
        if (static_cast<int>(i) == special) {
            unsigned zeros;
            if (!bits.get(2, zeros) || zeros > used - i) return false;
            i += zeros;
        }
    }
    tree = {};
    return tree.build(lengths);
}

bool readCodeLengths(Bits& bits, const Huffman& pt, Huffman& ctree) {
    unsigned used;
    if (!bits.get(CBits, used) || used > Nc) return false;
    std::vector<unsigned> lengths(Nc, 0);
    if (used == 0) {
        unsigned symbol;
        if (!bits.get(CBits, symbol) || symbol >= Nc) return false;
        ctree = {};
        ctree.isConstant = true;
        ctree.constant = symbol;
        return true;
    }
    unsigned i = 0;
    while (i < used) {
        unsigned code;
        if (!pt.decode(bits, code)) return false;
        if (code <= 2) {
            unsigned zeros = 0;
            if (code == 0) zeros = 1;
            else if (code == 1) {
                unsigned value;
                if (!bits.get(4, value)) return false;
                zeros = value + 3;
            } else {
                unsigned value;
                if (!bits.get(9, value)) return false;
                zeros = value + 20;
            }
            if (zeros > used - i) return false;
            i += zeros;
        } else {
            lengths[i++] = code - 2;
        }
    }
    ctree = {};
    return ctree.build(lengths);
}
} // namespace

bool decodeLh5Stream(const Lh5ByteReader& reader, std::size_t outputSize,
                     const Lh5OutputSink& sink, const char** error) {
    if (error) *error = "invalid LH5 stream";
    if (outputSize > 1536u * 1024u) {
        if (error) *error = "LH5 data exceeds the supported output limit";
        return false;
    }
    Bits bits(reader);
    std::size_t produced = 0;
    // Output is handed to the sink in chunks; only the 8 KiB dictionary is
    // retained. It lives on the heap because this runs on the BASIC task stack.
    std::vector<std::uint8_t> chunk;
    chunk.reserve(512);
    const auto flush = [&] {
        if (!chunk.empty()) sink(chunk.data(), chunk.size());
        chunk.clear();
    };
    const auto emit = [&](std::uint8_t byte) {
        chunk.push_back(byte);
        ++produced;
        if (chunk.size() == 512) flush();
    };
    std::vector<std::uint8_t> dictionary(DictionarySize, 0x20);
    unsigned write = DictionarySize - MaxMatch;
    Huffman pt, ctree, position;
    while (produced < outputSize) {
        unsigned blockSize;
        if (!bits.get(16, blockSize) || blockSize == 0) {
            if (error) *error = "truncated LH5 block";
            return false;
        }
        if (!readPtLengths(bits, Nt, TBits, 3, pt) ||
            !readCodeLengths(bits, pt, ctree) ||
            !readPtLengths(bits, Np, PBits, -1, position)) {
            if (error) *error = "invalid LH5 Huffman table";
            return false;
        }
        while (blockSize-- != 0 && produced < outputSize) {
            unsigned code;
            if (!ctree.decode(bits, code)) {
                if (error) *error = "truncated LH5 code";
                return false;
            }
            if (code < 256) {
                const auto byte = static_cast<std::uint8_t>(code);
                emit(byte);
                dictionary[write] = byte;
                write = (write + 1) & (DictionarySize - 1);
                continue;
            }
            const unsigned length = code - 256 + Threshold;
            unsigned pcode;
            if (length < Threshold || length > MaxMatch ||
                !position.decode(bits, pcode) || pcode >= Np) {
                if (error) *error = "invalid LH5 match";
                return false;
            }
            unsigned distance = pcode == 0 ? 0 : (1u << (pcode - 1));
            if (pcode != 0) {
                unsigned extra;
                if (!bits.get(pcode - 1, extra)) {
                    if (error) *error = "truncated LH5 position";
                    return false;
                }
                distance += extra;
            }
            for (unsigned i = 0; i < length && produced < outputSize; ++i) {
                const unsigned source = (write - distance - 1) & (DictionarySize - 1);
                const auto byte = dictionary[source];
                emit(byte);
                dictionary[write] = byte;
                write = (write + 1) & (DictionarySize - 1);
            }
        }
    }
    flush();
    if (error) *error = nullptr;
    return true;
}

bool decodeLh5(const std::uint8_t* input, std::size_t inputSize,
               std::size_t outputSize, std::vector<std::uint8_t>& output,
               const char** error) {
    if ((!input && inputSize != 0) || outputSize > 1536u * 1024u) {
        if (error) *error = "LH5 data exceeds the supported output limit";
        return false;
    }
    std::size_t consumed = 0;
    const Lh5ByteReader reader = [&](std::uint8_t* destination, std::size_t capacity) {
        const std::size_t count = std::min(capacity, inputSize - consumed);
        if (count != 0) std::copy_n(input + consumed, count, destination);
        consumed += count;
        return count;
    };
    std::vector<std::uint8_t> decoded;
    decoded.reserve(outputSize);
    const Lh5OutputSink sink = [&](const std::uint8_t* bytes, std::size_t count) {
        decoded.insert(decoded.end(), bytes, bytes + count);
    };
    if (!decodeLh5Stream(reader, outputSize, sink, error)) return false;
    output.swap(decoded);
    return true;
}

} // namespace ql32::music
