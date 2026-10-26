// Module 5, Part 4. Compatible with the MHC1 files from our Part 3 assembler.
// Written with OpenAI ChatGPT assistance; see the accompanying README.
// Build: g++ -std=c++17 -Wall -Wextra -pedantic disassembler.cpp -o disassembler
// Run:   ./disassembler fizzbuzz.mhc-bin
// Or run without an argument to enter a filename at the prompt.

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Word = std::uint32_t;
using Bytes = std::vector<unsigned char>;

constexpr Word TEXT_BASE = 0x00400000;
constexpr Word DATA_BASE = 0x10010000;
constexpr Word MAX_SECTION = 4 * 1024 * 1024;
constexpr std::size_t HEADER_BYTES = 32;
constexpr std::size_t MAX_FILE_BYTES = HEADER_BYTES + 2 * MAX_SECTION;
const fs::path OUTPUT_FILE = "mips5FIzzBuzz.asm";

std::string hex(Word value, int width = 8) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return out.str();
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

void appendByte(Bytes& bytes, unsigned char value) {
    if (bytes.size() >= MAX_FILE_BYTES)
        throw std::runtime_error("file exceeds the MHC1 size limit (4 MiB per section)");
    bytes.push_back(value);
}

// .mhc-bin already contains actual bytes. Binary mode prevents newline
// translation on Windows, where a byte such as 0x0A must remain unchanged.
Bytes readBinary(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file");
    Bytes bytes;
    char character;
    while (input.get(character)) appendByte(bytes, static_cast<unsigned char>(character));
    if (!input.eof()) throw std::runtime_error("error reading binary input");
    return bytes;
}

// .mhc-ascii contains text digits. Pack every eight digits into one real byte.
// Whitespace is ignored, so LF, CRLF, spaces, and tabs are all accepted.
Bytes readAscii(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file");
    Bytes bytes;
    unsigned value = 0, bits = 0;
    std::size_t line = 1, column = 0;
    char character;
    while (input.get(character)) {
        ++column;
        if (character == '0' || character == '1') {
            value = (value << 1) | static_cast<unsigned>(character - '0');
            if (++bits == 8) {
                appendByte(bytes, static_cast<unsigned char>(value));
                value = bits = 0;
            }
        } else if (!std::isspace(static_cast<unsigned char>(character))) {
            throw std::runtime_error("line " + std::to_string(line) + ", column " +
                std::to_string(column) + ": expected a binary digit (0 or 1) or whitespace");
        }
        if (character == '\n') { ++line; column = 0; }
    }
    if (!input.eof()) throw std::runtime_error("error reading ASCII input");
    if (bits != 0) throw std::runtime_error("incomplete byte: number of binary digits must be divisible by 8");
    return bytes;
}

Bytes readInput(const std::string& filename) {
    std::string extension = fs::path(filename).extension().string();
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension == ".mhc-bin") return readBinary(filename);
    if (extension == ".mhc-ascii") return readAscii(filename);
    throw std::runtime_error("unsupported extension; choose a .mhc-bin or .mhc-ascii file");
}

// The header and instructions use big-endian byte order.
Word readWord(const Bytes& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::runtime_error("truncated 32-bit word at byte offset " + std::to_string(offset));
    return (static_cast<Word>(bytes[offset]) << 24) |
           (static_cast<Word>(bytes[offset + 1]) << 16) |
           (static_cast<Word>(bytes[offset + 2]) << 8) |
            static_cast<Word>(bytes[offset + 3]);
}

struct Header {
    Word textBase, textSize, dataBase, dataSize, entry;
};

Header readHeader(const Bytes& bytes) {
    if (bytes.size() < HEADER_BYTES) throw std::runtime_error("file is too short for the 32-byte MHC1 header");
    if (readWord(bytes, 0) != 0x4D484331) throw std::runtime_error("invalid magic: expected MHC1");
    if (readWord(bytes, 4) != 1) throw std::runtime_error("unsupported MHC1 version; expected 1");
    Header h{readWord(bytes, 8), readWord(bytes, 12), readWord(bytes, 16),
             readWord(bytes, 20), readWord(bytes, 24)};
    if (readWord(bytes, 28) != 1) throw std::runtime_error("unsupported execution profile; expected 1");
    if (h.textBase != TEXT_BASE || h.dataBase != DATA_BASE)
        throw std::runtime_error("unsupported section addresses; expected text 0x00400000 and data 0x10010000");
    if (h.textSize == 0 || h.textSize % 4 != 0)
        throw std::runtime_error("text size must be a nonzero multiple of four bytes");
    if (h.textSize > MAX_SECTION || h.dataSize > MAX_SECTION)
        throw std::runtime_error("section size exceeds the 4 MiB limit");
    const std::size_t expected = HEADER_BYTES + static_cast<std::size_t>(h.textSize) + h.dataSize;
    if (bytes.size() != expected)
        throw std::runtime_error("header declares " + std::to_string(expected) +
            " total bytes, but the file contains " + std::to_string(bytes.size()));
    if (h.entry < h.textBase || h.entry >= h.textBase + h.textSize || h.entry % 4 != 0)
        throw std::runtime_error("entry address must point to an instruction in the text section");
    return h;
}

std::string reg(Word index) {
    static const std::array<std::string, 32> names = {
        "$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
        "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
        "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra"
    };
    return names.at(index);
}

int signedImmediate(Word immediate) {
    return immediate < 0x8000 ? static_cast<int>(immediate) : static_cast<int>(immediate) - 65536;
}

struct Instruction {
    std::string text;       // For a branch/jump, its target label is appended later.
    bool hasTarget = false;
    Word target = 0;
};

// Decode real instructions only. Pseudoinstructions were expanded by Part 3.
Instruction decode(Word word, Word pc) {
    const Word op = word >> 26, rs = (word >> 21) & 31, rt = (word >> 16) & 31;
    const Word rd = (word >> 11) & 31, shamt = (word >> 6) & 31, funct = word & 63;
    const Word immediate = word & 0xffff;
    const std::string signedValue = std::to_string(signedImmediate(immediate));

    // Check unused fields too: silently discarding them would break byte equality.
    if (op == 0) {
        if (funct == 0 && rs == 0)
            return {"sll " + reg(rd) + ", " + reg(rt) + ", " + std::to_string(shamt)};
        if (funct == 8 && rt == 0 && rd == 0 && shamt == 0) return {"jr " + reg(rs)};
        if (word == 0x0000000C) return {"syscall"};
        if (funct == 26 && rd == 0 && shamt == 0) return {"div " + reg(rs) + ", " + reg(rt)};
        if ((funct == 16 || funct == 18) && rs == 0 && rt == 0 && shamt == 0)
            return {std::string(funct == 16 ? "mfhi " : "mflo ") + reg(rd)};
        if ((funct == 32 || funct == 33 || funct == 34) && shamt == 0) {
            const std::string name = funct == 32 ? "add " : (funct == 33 ? "addu " : "sub ");
            return {name + reg(rd) + ", " + reg(rs) + ", " + reg(rt)};
        }
    } else {
        if (op == 8 || op == 9)
            return {std::string(op == 8 ? "addi " : "addiu ") + reg(rt) + ", " + reg(rs) + ", " + signedValue};
        if (op == 13) return {"ori " + reg(rt) + ", " + reg(rs) + ", 0x" + hex(immediate, 4)};
        if (op == 15 && rs == 0) return {"lui " + reg(rt) + ", 0x" + hex(immediate, 4)};
        if (op == 35 || op == 43)
            return {std::string(op == 35 ? "lw " : "sw ") + reg(rt) + ", " + signedValue + "(" + reg(rs) + ")"};
        if (op == 4 || op == 5) {
            const auto target = static_cast<std::int64_t>(pc) + 4 + 4LL * signedImmediate(immediate);
            return {std::string(op == 4 ? "beq " : "bne ") + reg(rs) + ", " + reg(rt) + ", ",
                    true, static_cast<Word>(target)};
        }
        if (op == 2 || op == 3)
            return {op == 2 ? "j " : "jal ", true, ((pc + 4) & 0xf0000000u) | ((word & 0x03ffffffu) << 2)};
    }
    throw std::runtime_error("unsupported or invalid instruction 0x" + hex(word) +
        " at address 0x" + hex(pc) + " (file byte offset " +
        std::to_string(HEADER_BYTES + pc - TEXT_BASE) + ")");
}

void writeAssembly(const Bytes& bytes, const Header& h) {
    std::vector<Instruction> instructions;
    std::map<Word, std::string> labels;
    labels[h.entry] = "main";

    // First decode the entire text section and discover branch/jump labels.
    // All validation finishes before the existing output file is replaced.
    for (Word offset = 0; offset < h.textSize; offset += 4) {
        const Word pc = h.textBase + offset;
        const auto instruction = decode(readWord(bytes, HEADER_BYTES + offset), pc);
        if (instruction.hasTarget) {
            const Word target = instruction.target;
            if (target < h.textBase || target >= h.textBase + h.textSize || target % 4 != 0)
                throw std::runtime_error("branch/jump at 0x" + hex(pc) + " targets 0x" +
                    hex(target) + ", outside the instruction section");
            if (labels.find(target) == labels.end()) labels[target] = "L_" + hex(target);
        }
        instructions.push_back(instruction);
    }

    fs::path stage;
    for (unsigned i = 0; ; ++i) {
        stage = "fizzbuzz-disassemble-" + std::to_string(i);
        if (fs::create_directory(stage)) break;
    }
    try {
        std::ofstream output(stage / OUTPUT_FILE, std::ios::binary);
        if (!output) throw std::runtime_error("cannot create assembly output");
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << "# Reconstructed from MHC1 version 1.\n"
               << "# Text: 0x" << hex(h.textBase) << "; data: 0x" << hex(h.dataBase) << ".\n"
               << "# MARS/SPIM syscall conventions; delayed branching disabled.\n"
               << "# Entry: main. Original label names and comments are not stored.\n\n.data\n";

        // .byte preserves every data byte, including embedded/trailing zeros.
        // Data is never mistaken for instructions or padded to a word boundary.
        const std::size_t dataStart = HEADER_BYTES + h.textSize;
        for (Word i = 0; i < h.dataSize; ++i) {
            if (i % 16 == 0) output << "    .byte ";
            else output << ", ";
            output << "0x" << hex(bytes[dataStart + i], 2);
            if (i % 16 == 15 || i + 1 == h.dataSize) output << '\n';
        }
        output << "\n.text\n.globl main\n";
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const Word pc = h.textBase + static_cast<Word>(i * 4);
            const auto label = labels.find(pc);
            if (label != labels.end()) output << '\n' << label->second << ":\n";
            output << "    " << instructions[i].text;
            if (instructions[i].hasTarget) output << labels.at(instructions[i].target);
            output << '\n';
        }
        output.close();
        fs::copy_file(stage / OUTPUT_FILE, OUTPUT_FILE, fs::copy_options::overwrite_existing);
        fs::remove_all(stage);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        throw;
    }
    std::cout << "Created " << OUTPUT_FILE.string() << '\n'
              << "Decoded " << instructions.size() << " instructions and preserved "
              << h.dataSize << " data bytes.\n";
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: disassembler [file.mhc-bin | file.mhc-ascii]\n";
        return 1;
    }
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: disassembler [file.mhc-bin | file.mhc-ascii]\n"
                  << "With no filename, the program prompts for one.\n"
                  << "Output: mips5FIzzBuzz.asm in the current folder.\n";
        return 0;
    }
    std::string filename;
    try {
        if (argc == 2) filename = argv[1];
        else {
            std::cout << "Enter the file to disassemble: " << std::flush;
            if (!std::getline(std::cin, filename)) throw std::runtime_error("no filename entered");
            filename = trim(filename);
            // Accept a quoted path pasted into the prompt, including spaces.
            if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"')
                filename = filename.substr(1, filename.size() - 2);
        }
        if (filename.empty()) throw std::runtime_error("no filename entered");
        const Bytes bytes = readInput(filename);
        const Header header = readHeader(bytes);
        if (fs::exists(OUTPUT_FILE) && fs::equivalent(filename, OUTPUT_FILE))
            throw std::runtime_error("input and output must be different files");
        writeAssembly(bytes, header);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: ";
        if (!filename.empty()) std::cerr << filename << ": ";
        std::cerr << e.what() << '\n';
        return 1;
    }
}
