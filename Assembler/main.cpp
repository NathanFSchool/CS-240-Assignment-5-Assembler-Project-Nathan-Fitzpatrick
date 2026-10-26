// Module 5, Part 3: a small two-pass MIPS assembler.
// Written with OpenAI ChatGPT assistance. See README.txt for sources and format.
// Build: g++ -std=c++17 -Wall -Wextra -pedantic assembler.cpp -o assembler
// Run:   ./assembler mips5FIzzBuzz.asm

#include <array>
#include <bitset>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Word = std::uint32_t;
using Bytes = std::vector<unsigned char>;

constexpr Word TEXT_BASE = 0x00400000;
constexpr Word DATA_BASE = 0x10010000;
constexpr Word MAX_SECTION = 4 * 1024 * 1024;
constexpr Word HEADER_BYTES = 32;

enum class Section { None, Text, Data };
struct Symbol { Word address; Section section; };
struct Reference { std::string name; Word pc; bool branch; bool code; int line; };
struct Line {
    std::vector<std::string> labels;
    std::string op;
    std::vector<std::string> args;
};

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

bool identifier(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    for (unsigned char c : s)
        if (!std::isalnum(c) && c != '_') return false;
    return true;
}

// Find a delimiter only outside quoted strings and character literals.
std::size_t outside(const std::string& s, char delimiter) {
    char quote = 0;
    bool escaped = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
        } else if (c == delimiter) return i;
        else if (c == '\'' || c == '"') quote = c;
    }
    if (quote) throw std::runtime_error("unterminated quoted literal");
    return std::string::npos;
}

Line parse(std::string s) {
    s = trim(s.substr(0, outside(s, '#')));
    Line result;
    for (;;) {
        const auto colon = outside(s, ':');
        if (colon == std::string::npos) break;
        const std::string label = trim(s.substr(0, colon));
        if (!identifier(label)) throw std::runtime_error("invalid label '" + label + "'");
        result.labels.push_back(label);
        s = trim(s.substr(colon + 1));
    }
    if (s.empty()) return result;
    const auto split = s.find_first_of(" \t");
    result.op = s.substr(0, split);
    if (split == std::string::npos) return result;
    s = trim(s.substr(split));
    if (s.empty()) return result;
    for (;;) {
        const auto comma = outside(s, ',');
        const std::string arg = trim(s.substr(0, comma));
        if (arg.empty()) throw std::runtime_error("empty operand");
        result.args.push_back(arg);
        if (comma == std::string::npos) break;
        s = trim(s.substr(comma + 1));
    }
    return result;
}

Bytes literal(const std::string& s, char quote) {
    if (s.size() < 2 || s.front() != quote || s.back() != quote)
        throw std::runtime_error("expected a quoted literal: " + s);
    Bytes result;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == static_cast<unsigned char>(quote))
            throw std::runtime_error("unexpected quote inside literal");
        if (c == '\\') {
            if (++i >= s.size() - 1) throw std::runtime_error("incomplete escape sequence");
            switch (s[i]) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '0': c = 0; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                case '"': c = '"'; break;
                default: throw std::runtime_error("unsupported escape sequence");
            }
        }
        result.push_back(c);
    }
    return result;
}

std::int64_t number(const std::string& s) {
    if (!s.empty() && s.front() == '\'') {
        const auto bytes = literal(s, '\'');
        if (bytes.size() != 1) throw std::runtime_error("character literal must contain one byte");
        return bytes[0];
    }
    try {
        std::size_t used = 0;
        const auto start = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1u : 0u;
        const bool hex = s.size() >= start + 2 && s[start] == '0' &&
                         (s[start + 1] == 'x' || s[start + 1] == 'X');
        const auto value = std::stoll(s, &used, hex ? 16 : 10);
        if (used != s.size()) throw std::invalid_argument("suffix");
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer '" + s + "'");
    }
}

std::int64_t bounded(const std::string& s, std::int64_t low, std::int64_t high) {
    const auto value = number(s);
    if (value < low || value > high)
        throw std::runtime_error("value '" + s + "' must be in [" +
                                 std::to_string(low) + ", " + std::to_string(high) + "]");
    return value;
}

Word reg(const std::string& s) {
    static const std::array<std::string, 32> names = {
        "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
        "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
        "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
    };
    for (Word i = 0; i < names.size(); ++i)
        if (s == names[i] || s == "$" + std::to_string(i)) return i;
    if (s == "$s8") return 30;
    throw std::runtime_error("invalid register '" + s + "'");
}

void operands(const Line& line, std::size_t count) {
    if (line.args.size() != count)
        throw std::runtime_error(line.op + " expects " + std::to_string(count) + " operand(s)");
}

// Pack the fields of the R and I instruction formats.
Word rType(Word rs, Word rt, Word rd, Word shamt, Word funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
}
Word iType(Word opcode, Word rs, Word rt, std::int64_t immediate) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | (static_cast<Word>(immediate) & 0xffffu);
}

std::vector<Word> loadImmediate(Word rd, std::int64_t value) {
    if (value >= -32768 && value <= 32767) return {iType(9, 0, rd, value)};
    if (value >= 0 && value <= 65535) return {iType(13, 0, rd, value)};
    const Word bits = static_cast<Word>(value);
    return {iType(15, 0, rd, bits >> 16), iType(13, rd, rd, bits & 0xffffu)};
}

class Assembler {
    std::string filename;
    std::map<std::string, Symbol> symbols;
    std::vector<Reference> references;
    Section section = Section::None;
    Word textSize = 0, dataSize = 0, expectedText = 0, expectedData = 0;
    Word entry = TEXT_BASE;
    int lineNumber = 0;
    bool firstPass = true;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(filename + ":" + std::to_string(lineNumber) + ": " + message);
    }

    Word target(const std::string& name, Word pc, bool branch, bool code) {
        if (!identifier(name)) throw std::runtime_error("expected a label, got '" + name + "'");
        if (firstPass) {
            references.push_back({name, pc, branch, code, lineNumber});
            return pc + 4; // Placeholder only; pass 1 determines sizes, not final encodings.
        }
        const auto found = symbols.find(name);
        if (found == symbols.end()) throw std::runtime_error("undefined label '" + name + "'");
        return found->second.address;
    }

    std::vector<Word> encode(const Line& line, Word pc) {
        const auto& op = line.op;
        const auto& a = line.args;
        if (op == "add" || op == "addu" || op == "sub") {
            operands(line, 3);
            const Word function = op == "add" ? 32 : (op == "addu" ? 33 : 34);
            return {rType(reg(a[1]), reg(a[2]), reg(a[0]), 0, function)};
        }
        if (op == "addi" || op == "addiu" || op == "ori") {
            operands(line, 3);
            const Word opcode = op == "addi" ? 8 : (op == "addiu" ? 9 : 13);
            const auto imm = bounded(a[2], op == "ori" ? 0 : -32768, op == "ori" ? 65535 : 32767);
            return {iType(opcode, reg(a[1]), reg(a[0]), imm)};
        }
        if (op == "lui") {
            operands(line, 2);
            return {iType(15, 0, reg(a[0]), bounded(a[1], 0, 65535))};
        }
        if (op == "div") {
            operands(line, 2);
            return {rType(reg(a[0]), reg(a[1]), 0, 0, 26)};
        }
        if (op == "mfhi" || op == "mflo") {
            operands(line, 1);
            return {rType(0, 0, reg(a[0]), 0, op == "mfhi" ? 16 : 18)};
        }
        if (op == "beq" || op == "bne") {
            operands(line, 3);
            const Word address = target(a[2], pc, true, true);
            const auto offset = (static_cast<std::int64_t>(address) - (pc + 4)) / 4;
            return {iType(op == "beq" ? 4 : 5, reg(a[0]), reg(a[1]), offset)};
        }
        if (op == "j" || op == "jal") {
            operands(line, 1);
            const Word address = target(a[0], pc, false, true);
            return {((op == "j" ? 2u : 3u) << 26) | ((address >> 2) & 0x03ffffffu)};
        }
        if (op == "jr") {
            operands(line, 1);
            return {rType(reg(a[0]), 0, 0, 0, 8)};
        }
        if (op == "lw" || op == "sw") {
            operands(line, 2);
            const auto left = a[1].find('('), right = a[1].find(')');
            if (left == std::string::npos || right != a[1].size() - 1 || left >= right)
                throw std::runtime_error("expected offset(base-register), such as 0($sp)");
            const auto offset = bounded(trim(a[1].substr(0, left)), -32768, 32767);
            const Word base = reg(trim(a[1].substr(left + 1, right - left - 1)));
            return {iType(op == "lw" ? 35 : 43, base, reg(a[0]), offset)};
        }
        if (op == "sll") {
            operands(line, 3);
            return {rType(0, reg(a[1]), reg(a[0]), static_cast<Word>(bounded(a[2], 0, 31)), 0)};
        }
        if (op == "syscall" || op == "nop") {
            operands(line, 0);
            return {op == "syscall" ? 12u : 0u};
        }
        if (op == "move") {
            operands(line, 2);
            return {rType(reg(a[1]), 0, reg(a[0]), 0, 33)};
        }
        if (op == "li") {
            operands(line, 2);
            return loadImmediate(reg(a[0]), bounded(a[1], -2147483648LL, 4294967295LL));
        }
        if (op == "la") {
            operands(line, 2);
            const Word rd = reg(a[0]), address = target(a[1], pc, false, false);
            return {iType(15, 0, rd, address >> 16), iType(13, rd, rd, address & 0xffffu)};
        }
        throw std::runtime_error("unsupported instruction '" + op + "'");
    }

    static void writeByte(std::ofstream& binary, unsigned char byte) {
        binary.write(reinterpret_cast<const char*>(&byte), 1);
    }
    static void writeWord(std::ofstream& binary, std::ofstream& ascii, Word word) {
        // Explicit shifts make the output big-endian on every host computer.
        for (int shift = 24; shift >= 0; shift -= 8)
            writeByte(binary, static_cast<unsigned char>((word >> shift) & 0xffu));
        ascii << std::bitset<32>(word) << '\n';
    }

    // Read and handle ONE source line at a time, in both passes.
    void pass(std::ifstream& input, std::ofstream* binary, std::ofstream* ascii) {
        section = Section::None;
        textSize = dataSize = 0;
        lineNumber = 0;
        std::string source;
        while (std::getline(input, source)) {
            ++lineNumber;
            try {
                const Line line = parse(source);
                for (const auto& label : line.labels) {
                    if (section == Section::None) throw std::runtime_error("label needs .text or .data first");
                    const Word address = section == Section::Text ? TEXT_BASE + textSize : DATA_BASE + dataSize;
                    if (firstPass) {
                        if (!symbols.emplace(label, Symbol{address, section}).second)
                            throw std::runtime_error("duplicate label '" + label + "'");
                    } else if (symbols.at(label).address != address || symbols.at(label).section != section)
                        throw std::runtime_error("source changed between passes");
                }
                if (line.op.empty()) continue;
                if (line.op == ".text" || line.op == ".data") {
                    operands(line, 0);
                    if (!line.labels.empty()) throw std::runtime_error("put section directives on their own line");
                    section = line.op == ".text" ? Section::Text : Section::Data;
                    continue;
                }
                if (line.op == ".globl") {
                    operands(line, 1);
                    target(line.args[0], TEXT_BASE, false, false);
                    continue;
                }
                if (line.op == ".asciiz" || line.op == ".ascii" || line.op == ".byte") {
                    if (section != Section::Data) throw std::runtime_error(line.op + " must be in .data");
                    if (line.args.empty()) throw std::runtime_error(line.op + " needs at least one operand");
                    Bytes bytes;
                    for (const auto& arg : line.args) {
                        if (line.op == ".byte") bytes.push_back(static_cast<unsigned char>(bounded(arg, -128, 255)));
                        else {
                            const auto value = literal(arg, '"');
                            bytes.insert(bytes.end(), value.begin(), value.end());
                            if (line.op == ".asciiz") bytes.push_back(0);
                        }
                    }
                    if (bytes.size() > MAX_SECTION - dataSize) throw std::runtime_error("data section exceeds 4 MiB");
                    if (!firstPass) {
                        // The header always comes first, then text, then data,
                        // even when .data appears before .text in the source.
                        binary->seekp(HEADER_BYTES + expectedText + dataSize);
                        ascii->seekp(8 * 33 + (expectedText / 4) * 33 + dataSize * 9);
                        for (unsigned char byte : bytes) {
                            writeByte(*binary, byte);
                            *ascii << std::bitset<8>(byte) << '\n';
                        }
                    }
                    dataSize += static_cast<Word>(bytes.size());
                    continue;
                }
                if (line.op[0] == '.') throw std::runtime_error("unsupported directive '" + line.op + "'");
                if (section != Section::Text) throw std::runtime_error("instruction must be in .text");
                const auto words = encode(line, TEXT_BASE + textSize);
                if (words.size() * 4 > MAX_SECTION - textSize) throw std::runtime_error("text section exceeds 4 MiB");
                if (!firstPass) {
                    binary->seekp(HEADER_BYTES + textSize);
                    ascii->seekp(8 * 33 + (textSize / 4) * 33);
                    std::cout << trim(source) << " == ";
                    for (std::size_t i = 0; i < words.size(); ++i) {
                        writeWord(*binary, *ascii, words[i]);
                        if (i) std::cout << " | ";
                        std::cout << std::bitset<32>(words[i]);
                    }
                    std::cout << '\n';
                }
                textSize += static_cast<Word>(words.size() * 4);
            } catch (const std::exception& e) { fail(e.what()); }
        }
        if (input.bad()) fail("error reading input file");
    }

    void validateReferences() {
        if (textSize == 0) fail("no instructions in .text");
        for (const auto& ref : references) {
            lineNumber = ref.line;
            const auto found = symbols.find(ref.name);
            if (found == symbols.end()) fail("undefined label '" + ref.name + "'");
            const auto& symbol = found->second;
            if (!ref.code) continue;
            if (symbol.section != Section::Text || symbol.address >= TEXT_BASE + textSize)
                fail("branch/jump target '" + ref.name + "' does not name an instruction");
            if (ref.branch) {
                const auto offset = (static_cast<std::int64_t>(symbol.address) - (ref.pc + 4)) / 4;
                if (offset < -32768 || offset > 32767) fail("branch target '" + ref.name + "' is out of range");
            } else if ((symbol.address & 0xf0000000u) != ((ref.pc + 4) & 0xf0000000u))
                fail("jump target is outside the current 256 MiB region");
        }
        const auto main = symbols.find("main");
        if (main != symbols.end()) {
            if (main->second.section != Section::Text || main->second.address >= TEXT_BASE + textSize)
                fail("main must name an instruction in .text");
            entry = main->second.address;
        }
    }

public:
    explicit Assembler(std::string path) : filename(std::move(path)) {}

    void assemble() {
        const fs::path asciiPath = "fizzbuzz.mhc-ascii", binaryPath = "fizzbuzz.mhc-bin";
        for (const auto& path : {asciiPath, binaryPath})
            if (fs::exists(path) && fs::equivalent(filename, path))
                throw std::runtime_error("input file cannot also be an output file");
        std::ifstream input(filename);
        if (!input) throw std::runtime_error("cannot open input file '" + filename + "'");

        // Pass 1: learn sizes and addresses, and validate the entire source.
        pass(input, nullptr, nullptr);
        validateReferences();
        expectedText = textSize;
        expectedData = dataSize;

        // Stage output so invalid assembly never replaces existing output files.
        fs::path stage;
        for (unsigned i = 0; ; ++i) {
            stage = "fizzbuzz-build-" + std::to_string(i);
            if (fs::create_directory(stage)) break;
        }
        try {
            // Binary mode is also used for the text stream to keep LF line
            // endings exactly one byte on Windows; this makes seekp portable.
            std::ofstream binary(stage / binaryPath, std::ios::binary);
            std::ofstream ascii(stage / asciiPath, std::ios::binary);
            if (!binary || !ascii) throw std::runtime_error("cannot create output files");
            binary.exceptions(std::ios::badbit | std::ios::failbit);
            ascii.exceptions(std::ios::badbit | std::ios::failbit);

            // Eight 32-bit header words. See README.txt for their exact layout.
            const std::array<Word, 8> header = {
                0x4d484331, 1, TEXT_BASE, expectedText, DATA_BASE, expectedData, entry, 1
            };
            for (Word value : header) writeWord(binary, ascii, value);

            // Pass 2: rewind, translate each line, and write its bytes directly.
            input.clear();
            input.seekg(0);
            if (!input) throw std::runtime_error("cannot rewind input file");
            firstPass = false;
            pass(input, &binary, &ascii);
            if (textSize != expectedText || dataSize != expectedData)
                throw std::runtime_error("source changed between passes");
            binary.close();
            ascii.close();
            fs::copy_file(stage / asciiPath, asciiPath, fs::copy_options::overwrite_existing);
            fs::copy_file(stage / binaryPath, binaryPath, fs::copy_options::overwrite_existing);
            fs::remove_all(stage);
        } catch (...) {
            std::error_code ignored;
            fs::remove_all(stage, ignored);
            throw;
        }
        std::cout << "Created fizzbuzz.mhc-ascii and fizzbuzz.mhc-bin\n"
                  << "Text: " << textSize / 4 << " instructions (" << textSize << " bytes)\n"
                  << "Data: " << dataSize << " bytes; header: " << HEADER_BYTES << " bytes\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: assembler [source.asm]\n";
        return 1;
    }
    try {
        Assembler assembler(argc == 2 ? argv[1] : "mips5FIzzBuzz.asm");
        assembler.assemble();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}