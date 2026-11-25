#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ql32 {

// Stored BASIC program: one text buffer plus a sorted 12-byte index. A
// std::map<int, std::string> costs about 90 bytes of node and string overhead
// per line on the ESP32; this costs 12. Replaced or deleted lines leave dead
// text that is compacted once it exceeds half of the buffer.
//
// Views returned by iteration stay valid until the next modification.
class ProgramStore {
public:
    using Line = std::pair<int, std::string_view>;

    class const_iterator {
    public:
        struct Arrow {
            Line line;
            const Line* operator->() const { return &line; }
        };
        const_iterator() = default;
        const_iterator(const ProgramStore* store, std::size_t index)
            : store_(store), index_(index) {}
        Line operator*() const { return store_->lineAt(index_); }
        Arrow operator->() const { return {store_->lineAt(index_)}; }
        const_iterator& operator++() { ++index_; return *this; }
        const_iterator& operator--() { --index_; return *this; }
        bool operator==(const const_iterator& other) const { return index_ == other.index_; }
        bool operator!=(const const_iterator& other) const { return index_ != other.index_; }

    private:
        const ProgramStore* store_ = nullptr;
        std::size_t index_ = 0;
    };

    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, index_.size()}; }
    std::size_t size() const { return index_.size(); }
    bool empty() const { return index_.empty(); }

    const_iterator find(int number) const {
        const auto it = lowerBound(number);
        return it != index_.end() && it->number == number ? const_iterator(this, position(it))
                                                          : end();
    }

    void set(int number, std::string_view body) {
        auto it = lowerBound(number);
        if (it != index_.end() && it->number == number) {
            dead_ += it->length;
            it->offset = static_cast<std::uint32_t>(text_.size());
            it->length = static_cast<std::uint32_t>(body.size());
            text_.append(body);
        } else {
            const Entry entry{number, static_cast<std::uint32_t>(text_.size()),
                              static_cast<std::uint32_t>(body.size())};
            text_.append(body);
            index_.insert(it, entry);
        }
        compactIfSparse();
    }

    void erase(int number) {
        const auto it = lowerBound(number);
        if (it == index_.end() || it->number != number) return;
        dead_ += it->length;
        index_.erase(it);
        compactIfSparse();
    }

    void clear() {
        index_.clear();
        text_.clear();
        text_.shrink_to_fit();
        index_.shrink_to_fit();
        dead_ = 0;
    }

    void swap(ProgramStore& other) {
        index_.swap(other.index_);
        text_.swap(other.text_);
        std::swap(dead_, other.dead_);
    }

    // Size of the "<number> <body>\n" listing, for exact-size joins.
    std::size_t listingBytes() const {
        std::size_t bytes = 0;
        for (const Entry& entry : index_)
            bytes += digits(entry.number) + 2 + entry.length;
        return bytes;
    }

    std::size_t retainedBytes() const {
        return index_.capacity() * sizeof(Entry) + text_.capacity();
    }

private:
    struct Entry {
        int number;
        std::uint32_t offset;
        std::uint32_t length;
    };

    static std::size_t digits(int number) {
        std::size_t count = 1;
        while (number >= 10) { number /= 10; ++count; }
        return count;
    }

    Line lineAt(std::size_t index) const {
        const Entry& entry = index_[index];
        return {entry.number, std::string_view(text_).substr(entry.offset, entry.length)};
    }
    std::vector<Entry>::iterator lowerBound(int number) {
        return std::lower_bound(index_.begin(), index_.end(), number,
                                [](const Entry& entry, int value) { return entry.number < value; });
    }
    std::vector<Entry>::const_iterator lowerBound(int number) const {
        return std::lower_bound(index_.begin(), index_.end(), number,
                                [](const Entry& entry, int value) { return entry.number < value; });
    }
    std::size_t position(std::vector<Entry>::const_iterator it) const {
        return static_cast<std::size_t>(it - index_.begin());
    }

    void compactIfSparse() {
        if (dead_ < 256 || dead_ * 2 < text_.size()) return;
        std::string packed;
        packed.reserve(text_.size() - dead_);
        for (Entry& entry : index_) {
            const auto offset = static_cast<std::uint32_t>(packed.size());
            packed.append(text_, entry.offset, entry.length);
            entry.offset = offset;
        }
        text_.swap(packed);
        dead_ = 0;
    }

    std::vector<Entry> index_;
    std::string text_;
    std::size_t dead_ = 0;
};

} // namespace ql32
