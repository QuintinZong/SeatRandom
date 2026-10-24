#pragma once

#include "SpreadsheetShuffler.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace seatrandom {

struct MatrixOptions {
    bool hasHeader = true;
    bool forceOverwrite = false;
    uint16_t nameColumn = 0;
    std::filesystem::path templatePath;
};

struct MatrixSheetReport {
    std::string name;
    uint32_t people = 0;
    uint32_t blocks = 0;
    bool skipped = false;
    std::string reason;
};

struct MatrixResult {
    std::vector<MatrixSheetReport> sheets;
};

MatrixResult writeMatrixWorkbook(std::filesystem::path const& shuffledWorkbook,
                                std::filesystem::path const& matrixWorkbook,
                                ShuffleResult const& shuffleResult,
                                MatrixOptions const& options);

}
