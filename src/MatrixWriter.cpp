#include "MatrixWriter.hpp"

#include <OpenXLSX.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace OpenXLSX;

namespace seatrandom {

namespace {

struct RoomSpec {
    const char* templateSheet;
    const char* sourceSheet;
    const char* label;
    uint32_t firstSeatRow;
    uint32_t lastSeatRow;
    uint16_t firstSeatColumn;
    uint16_t lastSeatColumn;
    uint32_t capacity;
};

constexpr RoomSpec Rooms[] = {
    {"全体", "全体", "2号楼101", 6, 11, 2, 7, 36},
    {"全体", "全体", "2号楼102", 15, 20, 2, 7, 36},
    {"全体", "全体", "2号楼103", 24, 29, 2, 7, 36},
    {"全体", "全体", "2号楼104", 34, 39, 2, 7, 36},
    {"全体", "全体", "2号楼105", 43, 49, 2, 7, 36},
    {"全体", "全体", "2号楼106", 53, 59, 2, 7, 36},
    {"全体", "全体", "1号楼101", 63, 70, 2, 7, 48},
    {"全体", "全体", "1号楼104", 74, 79, 2, 8, 42},
    {"全体", "全体", "1号楼109", 83, 91, 2, 6, 44},
    {"全体", "全体", "1号楼110", 95, 102, 2, 7, 42},
    {"全体", "全体", "新增30人考场", 106, 113, 2, 7, 30},
    {"全体", "全体", "新增20人考场", 117, 124, 2, 7, 20},

    {"外语", "英语", "2号楼101", 4, 10, 2, 7, 36},
    {"外语", "英语", "2号楼102", 14, 20, 2, 7, 36},
    {"外语", "英语", "2号楼103", 24, 30, 2, 7, 36},
    {"外语", "英语", "1号楼101", 34, 41, 2, 7, 42},
    {"外语", "英语", "1号楼104", 45, 50, 2, 8, 42},
    {"外语", "英语", "1号楼109", 54, 62, 2, 6, 44},
    {"外语", "英语", "1号楼110", 66, 73, 2, 7, 38},
    {"外语", "德语", "2号楼106", 77, 81, 2, 7, 28},
    {"外语", "日语", "2号楼104", 85, 90, 2, 7, 33},
    {"外语", "日语", "2号楼105", 94, 99, 2, 7, 33},
    {"外语", "零日", "零日", 103, 107, 2, 7, 28},
    {"外语", "法语", "法语", 111, 115, 2, 7, 15},
    {"外语", "零法", "零法", 119, 123, 2, 7, 11},
    {"外语", "西语", "西语", 127, 131, 2, 7, 11},
    {"外语", "零西", "零西", 135, 139, 2, 7, 9},
};

std::string pathToUtf8(std::filesystem::path const& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

uint16_t detectNameColumn(XLWorksheet& sheet, bool hasHeader, uint16_t columnCount) {
    if (hasHeader) {
        for (uint16_t column = 1; column <= columnCount; ++column) {
            auto cell = sheet.findCell(1, column);
            if (cell.empty()) continue;
            std::string header = cell.getString();
            if (header.find(u8"姓名") != std::string::npos) return column;
            if (lowerAscii(header).find("name") != std::string::npos) return column;
        }
    }
    return 1;
}

std::vector<std::string> readNames(XLWorksheet& sheet, uint32_t firstDataRow,
                                   uint32_t dataRows, uint16_t nameColumn) {
    std::vector<std::string> names;
    if (dataRows == 0) return names;

    uint32_t lastRow = firstDataRow + dataRows - 1;
    for (uint32_t row = firstDataRow; row <= lastRow; ++row) {
        auto cell = sheet.findCell(row, nameColumn);
        if (cell.empty()) continue;
        if (cell.value().type() == XLValueType::Empty) continue;
        std::string name = cell.getString();
        if (!name.empty()) names.push_back(std::move(name));
    }
    return names;
}

}

MatrixResult writeMatrixWorkbook(std::filesystem::path const& shuffledWorkbook,
                                std::filesystem::path const& matrixWorkbook,
                                ShuffleResult const& shuffleResult,
                                MatrixOptions const& options) {
    if (options.templatePath.empty()) {
        throw std::runtime_error("No template file specified");
    }

    XLDocument source;
    try {
        source.open(pathToUtf8(shuffledWorkbook));
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Can't open shuffled workbook: ") + error.what());
    }

    auto sourceWorkbook = source.workbook();

    std::map<std::string, std::vector<std::string>> namesBySheet;
    std::map<std::string, std::string> skippedSheets;

    for (auto const& report : shuffleResult.sheets) {
        if (report.skipped) {
            skippedSheets[report.name] = report.reason;
            continue;
        }

        auto sheet = sourceWorkbook.worksheet(report.name);

        uint16_t nameColumn = options.nameColumn;
        if (nameColumn == 0) {
            nameColumn = detectNameColumn(sheet, options.hasHeader,
                                          sheet.lastCell().column());
        }

        uint32_t firstDataRow = options.hasHeader ? 2u : 1u;
        namesBySheet[report.name] =
            readNames(sheet, firstDataRow, report.dataRows, nameColumn);
    }

    std::map<std::string, uint32_t> required;
    for (auto const& room : Rooms) {
        required[room.sourceSheet] += room.capacity;
    }

    for (auto const& entry : required) {
        auto const& sourceSheet = entry.first;
        uint32_t needed = entry.second;

        auto skipped = skippedSheets.find(sourceSheet);
        if (skipped != skippedSheets.end()) {
            throw std::runtime_error("Sheet " + sourceSheet + " was skipped: " +
                                     skipped->second);
        }

        auto names = namesBySheet.find(sourceSheet);
        if (names == namesBySheet.end()) {
            throw std::runtime_error("Missing source sheet in shuffled workbook: " +
                                     sourceSheet);
        }

        if (names->second.size() != needed) {
            throw std::runtime_error(
                "Sheet " + sourceSheet + " has " +
                std::to_string(names->second.size()) +
                " names, template requires " + std::to_string(needed));
        }
    }

    XLDocument target;
    try {
        target.open(pathToUtf8(options.templatePath));
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Can't open template file: ") + error.what());
    }

    auto targetWorkbook = target.workbook();

    std::map<std::string, size_t> cursors;
    std::map<std::string, MatrixSheetReport> reports;

    for (auto const& room : Rooms) {
        auto sheet = targetWorkbook.worksheet(room.templateSheet);
        auto& names = namesBySheet.at(room.sourceSheet);
        size_t& cursor = cursors[room.sourceSheet];

        uint64_t seatCells =
            static_cast<uint64_t>(room.lastSeatRow - room.firstSeatRow + 1) *
            static_cast<uint64_t>(room.lastSeatColumn - room.firstSeatColumn + 1);

        if (room.capacity > seatCells) {
            throw std::runtime_error(
                "Room " + std::string(room.label) +
                " capacity " + std::to_string(room.capacity) +
                " exceeds seat cells " + std::to_string(seatCells));
        }

        uint32_t remaining = room.capacity;
        for (uint32_t row = room.firstSeatRow;
             row <= room.lastSeatRow && remaining > 0; ++row) {
            for (uint16_t column = room.firstSeatColumn;
                 column <= room.lastSeatColumn && remaining > 0; ++column) {
                sheet.cell(row, column).value() = names[cursor++];
                --remaining;
            }
        }

        auto& report = reports[room.templateSheet];
        report.name = room.templateSheet;
        report.people += room.capacity;
        report.blocks += 1;
    }

    try {
        target.saveAs(pathToUtf8(matrixWorkbook),
                      options.forceOverwrite ? XLForceOverwrite : XLDoNotOverwrite);
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Can't write matrix file: ") + error.what());
    }

    MatrixResult result;
    for (auto const& entry : reports) {
        result.sheets.push_back(entry.second);
    }
    return result;
}

}
