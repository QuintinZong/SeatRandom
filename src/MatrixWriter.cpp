#include "MatrixWriter.hpp"

#include <OpenXLSX.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace OpenXLSX;

namespace seatrandom {

namespace {

constexpr uint16_t MatrixColumns = 6;
constexpr uint32_t LargeMatrixRows = 6;
constexpr uint32_t LargeMatrixCapacity = MatrixColumns * LargeMatrixRows;
constexpr uint32_t LargeMatrixThreshold = 100;
constexpr uint32_t MatrixGapRows = 3;
constexpr uint32_t LargeMatrixStride = LargeMatrixRows + MatrixGapRows;

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

XLStyleIndex makeMatrixFormat(XLDocument& document) {
    auto& styles = document.styles();

    auto borderIndex = styles.borders().create();
    auto border = styles.borders().borderByIndex(borderIndex);
    XLColor black(0, 0, 0);
    border.setLeft(XLLineStyleThin, black);
    border.setRight(XLLineStyleThin, black);
    border.setTop(XLLineStyleThin, black);
    border.setBottom(XLLineStyleThin, black);

    auto formatIndex = styles.cellFormats().create();
    auto format = styles.cellFormats().cellFormatByIndex(formatIndex);
    format.setBorderIndex(borderIndex);
    format.setApplyBorder(true);

    auto alignment = format.alignment(XLCreateIfMissing);
    alignment.setHorizontal(XLAlignCenter);
    alignment.setVertical(XLAlignCenter);
    format.setApplyAlignment(true);

    return formatIndex;
}

void applyCell(XLWorksheet& sheet, uint32_t row, uint16_t column,
               std::string const* name, XLStyleIndex formatIndex) {
    auto cell = sheet.cell(row, column);
    if (name != nullptr) cell.value() = *name;
    cell.setCellFormat(formatIndex);
}

void writeMatrix(XLWorksheet& sheet, std::vector<std::string> const& names,
                 XLStyleIndex formatIndex) {
    size_t count = names.size();
    if (count == 0) return;

    if (count >= LargeMatrixThreshold) {
        size_t blockCount = (count + LargeMatrixCapacity - 1) / LargeMatrixCapacity;
        for (size_t block = 0; block < blockCount; ++block) {
            uint32_t startRow = 1 + static_cast<uint32_t>(block) * LargeMatrixStride;
            size_t base = block * LargeMatrixCapacity;

            for (uint16_t column = 1; column <= MatrixColumns; ++column) {
                for (uint32_t row = 0; row < LargeMatrixRows; ++row) {
                    size_t index = base +
                                   static_cast<size_t>(column - 1) * LargeMatrixRows +
                                   row;
                    std::string const* name = index < count ? &names[index] : nullptr;
                    applyCell(sheet, startRow + row, column, name, formatIndex);
                }
            }

            for (uint32_t row = 0; row < LargeMatrixRows; ++row) {
                sheet.row(startRow + row).setHeight(22.0f);
            }
        }
    } else {
        uint32_t rows = static_cast<uint32_t>((count + MatrixColumns - 1) / MatrixColumns);

        for (uint16_t column = 1; column <= MatrixColumns; ++column) {
            for (uint32_t row = 0; row < rows; ++row) {
                size_t index = static_cast<size_t>(column - 1) * rows + row;
                std::string const* name = index < count ? &names[index] : nullptr;
                applyCell(sheet, row + 1, column, name, formatIndex);
            }
        }

        for (uint32_t row = 1; row <= rows; ++row) {
            sheet.row(row).setHeight(22.0f);
        }
    }

    for (uint16_t column = 1; column <= MatrixColumns; ++column) {
        sheet.column(column).setWidth(16.0f);
    }
}

}

MatrixResult writeMatrixWorkbook(std::filesystem::path const& shuffledWorkbook,
                                std::filesystem::path const& matrixWorkbook,
                                ShuffleResult const& shuffleResult,
                                MatrixOptions const& options) {
    MatrixResult result;

    XLDocument source;
    source.open(pathToUtf8(shuffledWorkbook));
    auto sourceWorkbook = source.workbook();

    struct SheetData {
        std::string name;
        std::vector<std::string> names;
    };

    std::vector<SheetData> sheetData;

    for (auto const& report : shuffleResult.sheets) {
        MatrixSheetReport matrixReport;
        matrixReport.name = report.name;

        if (report.skipped) {
            matrixReport.skipped = true;
            matrixReport.reason = report.reason;
            result.sheets.push_back(matrixReport);
            continue;
        }

        auto sheet = sourceWorkbook.worksheet(report.name);

        uint16_t nameColumn = options.nameColumn;
        if (nameColumn == 0) {
            nameColumn = detectNameColumn(sheet, options.hasHeader,
                                          sheet.lastCell().column());
        }

        uint32_t firstDataRow = options.hasHeader ? 2u : 1u;
        auto names = readNames(sheet, firstDataRow, report.dataRows, nameColumn);

        if (names.empty()) {
            matrixReport.skipped = true;
            matrixReport.reason = "no names";
            result.sheets.push_back(matrixReport);
            continue;
        }

        matrixReport.people = static_cast<uint32_t>(names.size());
        matrixReport.blocks = names.size() >= LargeMatrixThreshold
                                  ? static_cast<uint32_t>(
                                        (names.size() + LargeMatrixCapacity - 1) /
                                        LargeMatrixCapacity)
                                  : 1;
        result.sheets.push_back(matrixReport);

        sheetData.push_back({report.name, std::move(names)});
    }

    XLDocument target;
    target.create(pathToUtf8(matrixWorkbook),
                  options.forceOverwrite ? XLForceOverwrite : XLDoNotOverwrite);
    auto targetWorkbook = target.workbook();

    if (!sheetData.empty()) {
        auto firstSheet = targetWorkbook.worksheet(1);
        if (firstSheet.name() != sheetData.front().name) {
            firstSheet.setName(sheetData.front().name);
        }
        for (size_t index = 1; index < sheetData.size(); ++index) {
            targetWorkbook.addWorksheet(sheetData[index].name);
        }
    }

    auto formatIndex = makeMatrixFormat(target);

    for (auto const& data : sheetData) {
        auto sheet = targetWorkbook.worksheet(data.name);
        writeMatrix(sheet, data.names, formatIndex);
    }

    target.save();
    return result;
}

}
