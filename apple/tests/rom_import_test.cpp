#include "../../src/host/romset.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;
static void check(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static std::string read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main()
{
    char temporary[] = "/tmp/vf2-import-test-XXXXXX";
    const char *created = mkdtemp(temporary);
    check(created, "temporary directory");
    const fs::path root(created), destination = root / u8"ROM folder \u00e9" / "vf2.zip";
    struct cleanup { fs::path root; ~cleanup() { fs::remove_all(root); } } clean{root};
    fs::create_directories(destination.parent_path());
    const std::string original = "Existing file must survive every failed import.";
    std::ofstream(destination) << original;
    const auto rejected = [&](const fs::path &source) {
        auto result = romset::import_file(source.string(), destination.string());
        check(!result.ok && !result.error.empty(), "invalid import reports an error");
        check(read(destination) == original, "failed import preserves existing file");
        for (const auto &entry : fs::directory_iterator(destination.parent_path()))
            check(entry.path() == destination, "staging files cleaned up");
    };
    rejected(root / "missing.zip");
    std::ofstream(root / "short.zip") << "PK";
    rejected(root / "short.zip");
    std::ofstream(root / u8"invalid archive \u00e9.zip") << std::string(128, 'x');
    rejected(root / u8"invalid archive \u00e9.zip");
    const char empty_zip[22] = {'P', 'K', 5, 6};
    std::ofstream(root / "empty.zip", std::ios::binary).write(empty_zip, sizeof empty_zip);
    rejected(root / "empty.zip");
    std::ofstream(root / "large.zip");
    fs::resize_file(root / "large.zip", (256ull << 20) + 1);
    rejected(root / "large.zip");
    rejected(destination);
    std::cout << "ROM import failure, bounds, Unicode path and preservation checks passed.\n";
}
