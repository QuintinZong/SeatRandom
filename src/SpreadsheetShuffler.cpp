#include "SpreadsheetShuffler.hpp"
#include "Random.hpp"

#include <OpenXLSX.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace OpenXLSX;

namespace seatrandom {

namespace {

std::string pathToUtf8(std::filesystem::path const& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

bool rowIsEmpty(XLWorksheet& sheet, uint32_t row, uint16_t columnCount) {
    for (uint16_t column = 1; column <= columnCount; ++column) {
        auto cell = sheet.findCell(row, column);
        if (!cell.empty() && cell.value().type() != XLValueType::Empty) return false;
    }
    return true;
}

bool columnIsEmpty(XLWorksheet& sheet, uint32_t firstRow, uint32_t lastRow, uint16_t column) {
    for (uint32_t row = firstRow; row <= lastRow; ++row) {
        auto cell = sheet.findCell(row, column);
        if (!cell.empty() && cell.value().type() != XLValueType::Empty) return false;
    }
    return true;
}

bool hasDataFormula(XLWorksheet& sheet, uint32_t firstRow, uint32_t lastRow, uint16_t columnCount) {
    for (uint32_t row = firstRow; row <= lastRow; ++row) {
        for (uint16_t column = 1; column <= columnCount; ++column) {
            auto cell = sheet.findCell(row, column);
            if (!cell.empty() && cell.hasFormula()) return true;
        }
    }
    return false;
}

bool hasDataMerges(XLWorksheet& sheet, uint32_t firstDataRow) {
    auto& merges = sheet.merges();
    for (int32_t index = 0; index < merges.count(); ++index) {
        std::string reference = merges.merge(index);
        auto separator = reference.find(':');
        uint32_t bottomRow = 0;
        if (separator == std::string::npos) {
            bottomRow = XLCellReference(reference).row();
        } else {
            bottomRow = XLCellReference(reference.substr(separator + 1)).row();
        }
        if (bottomRow >= firstDataRow) return true;
    }
    return false;
}

void clearValueKeepStyle(XLCellAssignable& cell) {
    cell.clear(XLKeepCellStyle);
}

}

ShuffleResult shuffleWorkbook(std::filesystem::path const& input,
                              std::filesystem::path const& output,
                              ShuffleOptions const& options) {
    ShuffleResult result;
    result.seed = options.seed.value_or(makeBaseSeed());

    XLDocument document;
    try {
        document.open(pathToUtf8(input));
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Can't read the file: ") + error.what());
    }

    auto workbook = document.workbook();
    auto sheetNames = workbook.worksheetNames();

    std::vector<std::pair<std::string, size_t>> sheetsToProcess;
    if (options.selectedSheets.empty()) {
        for (size_t index = 0; index < sheetNames.size(); ++index) {
            sheetsToProcess.emplace_back(sheetNames[index], index);
        }
    } else {
        for (auto const& selected : options.selectedSheets) {
            size_t workbookIndex = 0;
            bool found = false;
            for (size_t index = 0; index < sheetNames.size(); ++index) {
                if (sheetNames[index] == selected) {
                    workbookIndex = index;
                    found = true;
                    break;
                }
            }
            if (!found) throw std::runtime_error("Sheet not found: " + selected);
            sheetsToProcess.emplace_back(selected, workbookIndex);
        }
    }

    for (auto const& entry : sheetsToProcess) {
        auto const& name = entry.first;
        size_t workbookIndex = entry.second;
        auto sheet = workbook.worksheet(name);

        SheetReport report;
        report.name = name;
        report.seed = deriveSheetSeed(result.seed, name, workbookIndex);

        auto lastCell = sheet.lastCell();
        uint32_t lastRow = lastCell.row();
        uint16_t lastColumn = lastCell.column();
        uint32_t firstDataRow = options.hasHeader ? 2u : 1u;

        if (lastRow < firstDataRow) {
            report.skipped = true;
            report.reason = "no data rows";
            result.sheets.push_back(report);
            continue;
        }

        while (lastRow >= firstDataRow && rowIsEmpty(sheet, lastRow, lastColumn)) --lastRow;

        if (lastRow < firstDataRow) {
            report.skipped = true;
            report.reason = "no data rows";
            result.sheets.push_back(report);
            continue;
        }

        while (lastColumn > 1 &&
               columnIsEmpty(sheet, firstDataRow, lastRow, lastColumn)) {
            --lastColumn;
        }

        if (hasDataMerges(sheet, firstDataRow)) {
            report.skipped = true;
            report.reason = "merged cells in data area";
            result.sheets.push_back(report);
            continue;
        }

        if (hasDataFormula(sheet, firstDataRow, lastRow, lastColumn)) {
            report.skipped = true;
            report.reason = "formulas in data area";
            result.sheets.push_back(report);
            continue;
        }

        std::vector<std::vector<XLCellValue>> data;
        data.reserve(lastRow - firstDataRow + 1);
        for (uint32_t row = firstDataRow; row <= lastRow; ++row) {
            std::vector<XLCellValue> values(lastColumn);
            for (uint16_t column = 1; column <= lastColumn; ++column) {
                auto cell = sheet.findCell(row, column);
                if (!cell.empty()) values[column - 1] = cell.value();
            }
            data.push_back(std::move(values));
        }

        report.dataRows = static_cast<uint32_t>(data.size());

        std::vector<size_t> order(data.size());
        std::iota(order.begin(), order.end(), 0);
        auto rng = makeSheetRng(report.seed);
        std::shuffle(order.begin(), order.end(), rng);

        for (size_t index = 0; index < order.size(); ++index) {
            uint32_t targetRow = firstDataRow + static_cast<uint32_t>(index);
            for (uint16_t column = 1; column <= lastColumn; ++column) {
                auto const& value = data[order[index]][column - 1];
                if (value.type() == XLValueType::Empty) {
                    auto target = sheet.findCell(targetRow, column);
                    if (!target.empty()) clearValueKeepStyle(target);
                } else {
                    auto target = sheet.cell(targetRow, column);
                    clearValueKeepStyle(target);
                    target.value() = value;
                }
            }
        }

        result.sheets.push_back(report);
    }

    try {
        document.saveAs(pathToUtf8(output),
                        options.forceOverwrite ? XLForceOverwrite : XLDoNotOverwrite);
    } catch (std::exception const& error) {
        throw std::runtime_error(std::string("Can't write the file: ") + error.what());
    }

    return result;
}

}
