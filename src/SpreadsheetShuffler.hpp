#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace seatrandom {

struct ShuffleOptions {
    bool hasHeader = true;
    bool forceOverwrite = false;
    std::optional<uint64_t> seed;
    std::vector<std::string> selectedSheets;
};

struct SheetReport {
    std::string name;
    uint32_t dataRows = 0;
    uint64_t seed = 0;
    bool skipped = false;
    std::string reason;
};

struct ShuffleResult {
    uint64_t seed = 0;
    std::vector<SheetReport> sheets;
};

ShuffleResult shuffleWorkbook(std::filesystem::path const& input,
                              std::filesystem::path const& output,
                              ShuffleOptions const& options);

}
