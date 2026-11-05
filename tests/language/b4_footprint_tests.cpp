#include "language/bytecode.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string numberedPrints(std::size_t count) {
    std::string source;
    for (std::size_t line = 1; line <= count; ++line)
        source += std::to_string(line * 10) + " PRINT " + std::to_string(line) + "\n";
    return source;
}

bool checkFootprint(const char* label, const std::string& source,
                    double maximumRatio, bool enforceCompact) {
    auto rich = ql32::detail::compileSource(source);
    const std::size_t richBytes = ql32::detail::estimateInstructionSequenceBytes(rich);
#ifdef QL32_TEST_COMPACT
    ql32::detail::CompactProgram compact(std::move(rich));
    const std::size_t compactBytes = compact.encodedStorageBytes();
    const double ratio = richBytes == 0 ? 0.0 :
        static_cast<double>(compactBytes) / static_cast<double>(richBytes);
    std::cout << label << ": rich=" << richBytes << " encoded=" << compactBytes
              << " ratio=" << ratio << '\n';
    if (enforceCompact && ratio > maximumRatio) {
        std::cerr << label << " exceeds the encoded footprint limit\n";
        return false;
    }
#else
    (void)label;
    (void)maximumRatio;
    (void)enforceCompact;
    if (richBytes == 0) return false;
#endif
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ql32_b4_footprint_tests <source-root>\n";
        return 2;
    }
    const std::string root = argv[1];
    if (!checkFootprint("tiny-10-line", numberedPrints(10), 0.50, true) ||
        !checkFootprint("synthetic-2000-line", numberedPrints(2000), 0.30, true) ||
        !checkFootprint("attractor", readFile(root + "/examples/attractor.bas"), 1.00, true) ||
        !checkFootprint("mixed-mandelbrot",
                        readFile(root + "/benchmarks/mandelbrot.bas"), 1.00, true))
        return 1;
}
