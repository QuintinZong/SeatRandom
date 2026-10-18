#include "SpreadsheetShuffler.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

#ifdef _WIN32

std::string toUtf8(std::wstring const& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Can't convert command line to UTF-8");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::vector<std::string> commandLineArguments(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wideArguments == nullptr) throw std::runtime_error("Can't read the command line");
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        arguments.push_back(toUtf8(wideArguments[index]));
    }
    LocalFree(wideArguments);
    return arguments;
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                      static_cast<DWORD>(buffer.size()));
    if (length == 0) return std::filesystem::current_path();
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

#else

std::vector<std::string> commandLineArguments(int argc, char** argv) {
    return std::vector<std::string>(argv, argv + argc);
}

std::filesystem::path executableDirectory() {
    return std::filesystem::current_path();
}

#endif

std::string lowercaseExtension(std::filesystem::path const& path) {
    auto extension = path.extension().u8string();
    std::string result(extension.begin(), extension.end());
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::filesystem::path findInputInExecutableDirectory() {
    auto directory = executableDirectory();
    std::vector<std::filesystem::path> candidates;

    for (auto const& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto fileName = entry.path().filename().u8string();
        if (fileName.rfind("~$", 0) == 0) continue;
        if (lowercaseExtension(entry.path()) == ".xlsx") {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.empty()) throw std::runtime_error("Can't find xlsx file");

    if (candidates.size() > 1) {
        std::ostringstream message;
        message << "Can't find xlsx file: multiple .xlsx files in "
                << directory.u8string();
        throw std::runtime_error(message.str());
    }

    return candidates.front();
}

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm* local = std::localtime(&value);
    if (local == nullptr) throw std::runtime_error("Can't read the current time");
    std::tm copy = *local;
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &copy);
    return buffer;
}

std::filesystem::path defaultOutputPath(std::filesystem::path const& input) {
    auto directory = input.parent_path();
    if (directory.empty()) directory = std::filesystem::current_path();

    auto stemValue = input.stem().u8string();
    std::string fileName(stemValue.begin(), stemValue.end());
    fileName += "_";
    fileName += timestamp();
    fileName += ".xlsx";

    return directory / "seat" / std::filesystem::u8path(fileName);
}

void printUsage() {
    std::cout
        << "Usage: seatrandom.exe <input.xlsx> [options]\n"
        << "  --out <path>       output .xlsx path\n"
        << "  --seed <number>    fixed random seed\n"
        << "  --no-header        shuffle all rows; default keeps row 1\n"
        << "  --sheet <name>     process only this sheet; repeatable\n"
        << "  --force            overwrite an existing output file\n"
        << "  --help             show this help\n";
}

struct CommandLineOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    std::optional<uint64_t> seed;
    bool hasHeader = true;
    bool force = false;
    bool help = false;
    std::vector<std::string> sheets;
};

uint64_t parseSeed(std::string const& text) {
    if (text.empty() || text.front() == '-') {
        throw std::runtime_error("Invalid seed: " + text);
    }

    try {
        size_t position = 0;
        uint64_t value = std::stoull(text, &position, 0);
        if (position != text.size()) {
            throw std::runtime_error("Invalid seed: " + text);
        }
        return value;
    } catch (std::exception const&) {
        throw std::runtime_error("Invalid seed: " + text);
    }
}

CommandLineOptions parseArguments(std::vector<std::string> const& arguments) {
    CommandLineOptions options;

    for (size_t index = 1; index < arguments.size(); ++index) {
        auto const& argument = arguments[index];

        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--no-header") {
            options.hasHeader = false;
        } else if (argument == "--header") {
            options.hasHeader = true;
        } else if (argument == "--force") {
            options.force = true;
        } else if (argument == "--out" || argument == "-o") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.output = std::filesystem::u8path(arguments[index]);
        } else if (argument == "--seed" || argument == "-s") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.seed = parseSeed(arguments[index]);
        } else if (argument == "--sheet") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.sheets.push_back(arguments[index]);
        } else if (!argument.empty() && argument.front() == '-') {
            throw std::runtime_error("Unknown option: " + argument);
        } else {
            if (!options.input.empty()) {
                throw std::runtime_error("Multiple input files specified");
            }
            options.input = std::filesystem::u8path(argument);
        }
    }

    if (options.help) return options;

    if (options.input.empty()) {
        options.input = findInputInExecutableDirectory();
    }

    if (!std::filesystem::is_regular_file(options.input)) {
        throw std::runtime_error("Can't find xlsx file: " + options.input.u8string());
    }

    if (lowercaseExtension(options.input) != ".xlsx") {
        throw std::runtime_error("Can't read the file: not an .xlsx file");
    }

    if (options.output.empty()) {
        options.output = defaultOutputPath(options.input);
    }

    if (std::filesystem::absolute(options.input).lexically_normal() ==
        std::filesystem::absolute(options.output).lexically_normal()) {
        throw std::runtime_error("Output path must differ from input path");
    }

    if (!options.force && std::filesystem::exists(options.output)) {
        throw std::runtime_error("Output file already exists: " +
                                 options.output.u8string() +
                                 " (use --force to overwrite)");
    }

    return options;
}

}

int main(int argc, char** argv) {
std::cout<<"Starting...\n";
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        //将控制台输出编码设置为UTF-8
    SetConsoleCP(CP_UTF8);
#endif

    try {
        auto arguments = commandLineArguments(argc, argv);
        auto options = parseArguments(arguments);

        if (options.help) {
            printUsage();
            return 0;
        }

        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(options.output.parent_path());
        }

        seatrandom::ShuffleOptions shuffleOptions;
        shuffleOptions.hasHeader = options.hasHeader;
        shuffleOptions.forceOverwrite = options.force;
        shuffleOptions.seed = options.seed;
        shuffleOptions.selectedSheets = options.sheets;

        auto result = seatrandom::shuffleWorkbook(options.input, options.output, shuffleOptions);

        std::cout << "Seed: " << result.seed << "\n";
        for (auto const& sheet : result.sheets) {
            if (sheet.skipped) {
                std::cout << "[skip] " << sheet.name << ": " << sheet.reason << "\n";
            } else {
                std::cout << "[ok] " << sheet.name << ": " << sheet.dataRows
                          << " rows, seed " << sheet.seed << "\n";
            }
        }
        std::cout << "Output: " << options.output.u8string() << "\n";
        return 0;
        system("pause");
    } catch (std::exception const& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
        system("pause");
    }
}
