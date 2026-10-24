#include "MatrixWriter.hpp"
#include "SpreadsheetShuffler.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
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
#include <commdlg.h>
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

std::filesystem::path openFileDialog() {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = nullptr;
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"Excel files (*.xlsx)\0*.xlsx\0All files (*.*)\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = L"Select the roster Excel file";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError() != 0) {
            throw std::runtime_error("Can't open the file dialog");
        }
        return {};
    }

    return std::filesystem::path(fileName);
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

std::filesystem::path findTemplateInExecutableDirectory() {
    auto directory = executableDirectory();
    std::vector<std::filesystem::path> candidates;

    for (auto const& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        if (lowercaseExtension(entry.path()) != ".xlsx") continue;
        auto name = entry.path().filename().u8string();
        if (name.find(u8"模板") != std::string::npos) {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.empty()) throw std::runtime_error("Can't find template xlsx");
    if (candidates.size() > 1) throw std::runtime_error("Multiple template xlsx files found");
    return candidates.front();
}

std::filesystem::path resolveInputFile() {
    auto directory = executableDirectory();
    std::vector<std::filesystem::path> candidates;

    for (auto const& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto fileName = entry.path().filename().u8string();
        if (fileName.rfind("~$", 0) == 0) continue;
        if (fileName.find(u8"模板") != std::string::npos) continue;
        if (lowercaseExtension(entry.path()) == ".xlsx") {
            candidates.push_back(entry.path());
        }
    }

    if (candidates.size() == 1) return candidates.front();

#ifdef _WIN32
    auto selected = openFileDialog();
    if (selected.empty()) {
        throw std::runtime_error("No file selected");
    }
    return selected;
#else
    if (candidates.empty()) {
        throw std::runtime_error("Can't find xlsx file");
    }
    throw std::runtime_error("Multiple .xlsx files found; specify one explicitly");
#endif
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
struct OutputPaths {
    std::filesystem::path random;
    std::filesystem::path matrix;
};

OutputPaths defaultOutputPaths(std::filesystem::path const& input) {
    auto stemValue = input.stem().u8string();
    std::string stem(stemValue.begin(), stemValue.end());
    std::string stamp = timestamp();
    std::string examtime_;
    auto folder = executableDirectory() / "seat";
    std::string randomName;
    std::string matrixName;
    std::cout << "请输入考试月份（1/2/3/4/5/6/9/10/11/12）：" << std::endl;
    std::cin>>examtime_;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (examtime_ == "3" || examtime_ == "10") {
        randomName = stem + "_第一次月考座位表" + "_random_" + stamp + ".xlsx";
        matrixName = stem + "_第一次月考座位表" + "_matrix_" + stamp + ".xlsx";
    }
    
    else if (examtime_ == "4" || examtime_ == "11") {
        randomName = stem + "_期中考试座位表" + "_random_" + stamp + ".xlsx";
        matrixName = stem + "_期中考试座位表" + "_matrix_" + stamp + ".xlsx";
    }
    
    else if (examtime_ == "5" || examtime_ == "12") {
        randomName = stem + "_第二次月考座位表" + "_random_" + stamp + ".xlsx";
        matrixName = stem + "_第二次月考座位表" + "_matrix_" + stamp + ".xlsx";
    }

    else if (examtime_ == "6" || examtime_ == "1") {
        randomName = stem + "_期末考试座位表" + "_random_" + stamp + ".xlsx";
        matrixName = stem + "_期末考试座位表" + "_matrix_" + stamp + ".xlsx";
    }

    else {
        randomName = stem + "_random_" + stamp + ".xlsx";
        matrixName = stem + "_matrix_" + stamp + ".xlsx";
    }

    return {folder / std::filesystem::u8path(randomName),
            folder / std::filesystem::u8path(matrixName)};
}

enum class PauseMode {
    Auto,
    Always,
    Never
};

PauseMode detectPauseMode(std::vector<std::string> const& arguments) {
    PauseMode mode = PauseMode::Auto;
    for (auto const& argument : arguments) {
        if (argument == "--pause") {
            mode = PauseMode::Always;
        } else if (argument == "--no-pause") {
            mode = PauseMode::Never;
        }
    }
    return mode;
}

bool shouldPause(PauseMode mode) {
    if (mode == PauseMode::Always) return true;
    if (mode == PauseMode::Never) return false;
#ifdef _WIN32
    DWORD processes[2] = {};
    return GetConsoleProcessList(processes, 2) == 1;
#else
    return false;
#endif
}

void waitForEnter(PauseMode mode) {
    if (!shouldPause(mode)) return;
    std::cout << "\nPress Enter to exit..." << std::flush;
    std::cin.get();
}

void printUsage() {
    std::cout
        << "Usage: seatrandom.exe <input.xlsx> [options]\n"
        << "  --out <path>        normal random output .xlsx path\n"
        << "  --matrix-out <path> matrix output .xlsx path\n"
        << "  --template <path>   template .xlsx path\n"
        << "  --name-column <col> name column, e.g. C; default auto-detect\n"
        << "  --seed <number>     fixed random seed\n"
        << "  --no-header         shuffle all rows; default keeps row 1\n"
        << "  --sheet <name>      process only this sheet; repeatable\n"
        << "  --force             overwrite existing output files\n"
        << "  --pause             keep the console open after exit\n"
        << "  --no-pause          never wait for Enter\n"
        << "  --help              show this help\n";
}

struct CommandLineOptions {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path matrixOutput;
    std::filesystem::path templatePath;
    std::optional<uint64_t> seed;
    bool hasHeader = true;
    bool force = false;
    bool help = false;
    PauseMode pauseMode = PauseMode::Auto;
    uint16_t nameColumn = 0;
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

uint16_t parseColumn(std::string const& text) {
    if (text.empty()) throw std::runtime_error("Invalid column: " + text);

    uint32_t value = 0;
    for (unsigned char c : text) {
        if (!std::isalpha(c)) throw std::runtime_error("Invalid column: " + text);
        value = value * 26 + static_cast<uint32_t>(std::toupper(c) - 'A' + 1);
    }

    if (value == 0 || value > 16384) {
        throw std::runtime_error("Invalid column: " + text);
    }
    return static_cast<uint16_t>(value);
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
        } else if (argument == "--pause") {
            options.pauseMode = PauseMode::Always;
        } else if (argument == "--no-pause") {
            options.pauseMode = PauseMode::Never;
        } else if (argument == "--out" || argument == "-o") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.output = std::filesystem::u8path(arguments[index]);
        } else if (argument == "--matrix-out") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.matrixOutput = std::filesystem::u8path(arguments[index]);
        } else if (argument == "--template") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.templatePath = std::filesystem::u8path(arguments[index]);
        } else if (argument == "--name-column") {
            if (++index >= arguments.size()) {
                throw std::runtime_error("Missing value for " + argument);
            }
            options.nameColumn = parseColumn(arguments[index]);
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
        options.input = resolveInputFile();
    }

    if (!std::filesystem::is_regular_file(options.input)) {
        throw std::runtime_error("Can't find xlsx file: " + options.input.u8string());
    }

    if (lowercaseExtension(options.input) != ".xlsx") {
        throw std::runtime_error("Can't read the file: not an .xlsx file");
    }

    if (options.templatePath.empty()) {
        options.templatePath = findTemplateInExecutableDirectory();
    }

    if (!std::filesystem::is_regular_file(options.templatePath)) {
        throw std::runtime_error("Can't find template file: " +
                                 options.templatePath.u8string());
    }

    if (lowercaseExtension(options.templatePath) != ".xlsx") {
        throw std::runtime_error("Can't read the template file: not an .xlsx file");
    }

    if (options.output.empty() && options.matrixOutput.empty()) {
        auto paths = defaultOutputPaths(options.input);
        options.output = paths.random;
        options.matrixOutput = paths.matrix;
    } else if (options.output.empty()) {
        options.output = defaultOutputPaths(options.input).random;
    } else if (options.matrixOutput.empty()) {
        auto directory = options.output.parent_path();
        auto stemValue = options.output.stem().u8string();
        std::string name(stemValue.begin(), stemValue.end());
        name += "_matrix";
        auto extension = options.output.extension();
        if (extension.empty()) {
            name += ".xlsx";
        } else {
            auto extensionValue = extension.u8string();
            name.append(extensionValue.begin(), extensionValue.end());
        }
        options.matrixOutput = directory / std::filesystem::u8path(name);
    }

    auto inputPath = std::filesystem::absolute(options.input).lexically_normal();
    auto randomPath = std::filesystem::absolute(options.output).lexically_normal();
    auto matrixPath = std::filesystem::absolute(options.matrixOutput).lexically_normal();
    auto templatePath = std::filesystem::absolute(options.templatePath).lexically_normal();

    if (randomPath == inputPath || matrixPath == inputPath) {
        throw std::runtime_error("Output path must differ from input path");
    }

    if (randomPath == matrixPath) {
        throw std::runtime_error("Random and matrix output paths must differ");
    }

    if (randomPath == templatePath || matrixPath == templatePath) {
        throw std::runtime_error("Output path must differ from template path");
    }

    if (!options.force && std::filesystem::exists(options.output)) {
        throw std::runtime_error("Output file already exists: " +
                                 options.output.u8string() +
                                 " (use --force to overwrite)");
    }

    if (!options.force && std::filesystem::exists(options.matrixOutput)) {
        throw std::runtime_error("Matrix output file already exists: " +
                                 options.matrixOutput.u8string() +
                                 " (use --force to overwrite)");
    }

    return options;
}

}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    PauseMode pauseMode = PauseMode::Auto;
    int exitCode = 0;

    try {
        auto arguments = commandLineArguments(argc, argv);
        pauseMode = detectPauseMode(arguments);
        auto options = parseArguments(arguments);
        pauseMode = options.pauseMode;

        if (options.help) {
            printUsage();
        } else {
            if (!options.output.parent_path().empty()) {
                std::filesystem::create_directories(options.output.parent_path());
            }
            if (!options.matrixOutput.parent_path().empty()) {
                std::filesystem::create_directories(options.matrixOutput.parent_path());
            }

            seatrandom::ShuffleOptions shuffleOptions;
            shuffleOptions.hasHeader = options.hasHeader;
            shuffleOptions.forceOverwrite = options.force;
            shuffleOptions.seed = options.seed;
            shuffleOptions.selectedSheets = options.sheets;

            auto result = seatrandom::shuffleWorkbook(options.input, options.output, shuffleOptions);

            seatrandom::MatrixOptions matrixOptions;
            matrixOptions.hasHeader = options.hasHeader;
            matrixOptions.forceOverwrite = options.force;
            matrixOptions.nameColumn = options.nameColumn;
            matrixOptions.templatePath = options.templatePath;

            auto matrixResult = seatrandom::writeMatrixWorkbook(
                options.output, options.matrixOutput, result, matrixOptions);

            std::cout << "Seed: " << result.seed << "\n";
            for (auto const& sheet : result.sheets) {
                if (sheet.skipped) {
                    std::cout << "[skip] " << sheet.name << ": " << sheet.reason << "\n";
                } else {
                    std::cout << "[ok] " << sheet.name << ": " << sheet.dataRows
                              << " rows, seed " << sheet.seed << "\n";
                }
            }
            for (auto const& sheet : matrixResult.sheets) {
                if (sheet.skipped) {
                    std::cout << "[matrix-skip] " << sheet.name << ": "
                              << sheet.reason << "\n";
                } else {
                    std::cout << "[matrix] " << sheet.name << ": " << sheet.people
                              << " people, " << sheet.blocks << " block(s)\n";
                }
            }
            std::cout << "Random output: " << options.output.u8string() << "\n";
            std::cout << "Matrix output: " << options.matrixOutput.u8string() << "\n";
        }
    } catch (std::exception const& error) {
        std::cerr << "Error: " << error.what() << "\n";
        exitCode = 1;
    }

    waitForEnter(pauseMode);
    return exitCode;
}
