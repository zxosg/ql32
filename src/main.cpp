#include "ql32/interpreter.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: ql32 [program.bas]\n";
        return 2;
    }

    std::string source;
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) {
            std::cerr << "ql32: cannot open " << argv[1] << '\n';
            return 2;
        }
        source.assign(std::istreambuf_iterator<char>(file), {});
    } else {
        source.assign(std::istreambuf_iterator<char>(std::cin), {});
    }

    try {
        ql32::Interpreter interpreter;
        if (argc == 2) interpreter.execute(source, std::cout);
        else interpreter.executeInteractive(source, std::cout);
    } catch (const std::exception& error) {
        std::cerr << "ql32: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
