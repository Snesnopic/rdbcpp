#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <exception>
#include <system_error>
#include <array>
#include <cstdlib>

namespace fs = std::filesystem;

// tencent rdb header magic: 531E98204F8542F0
constexpr std::array<uint8_t, 16> RDB_MAGIC = { 0x35, 0x33, 0x31, 0x45, 0x39, 0x38, 0x32, 0x30, 0x34, 0x46, 0x38, 0x35, 0x34, 0x32, 0x46, 0x30 };
constexpr uint64_t RDB_MIN_SIZE = 0x24; // header: 16 (magic) + 4 (file_num) + 8 (index_offset) + 8 (index_size)

struct RdbFileEntry {
    std::u16string name;
    std::vector<uint8_t> data;
};

void print_usage(std::ostream& os) {
    os << "usage: rdbcpp <--pack|--unpack> <input_path> <output_path>\n"
       << "  --pack:   creates an .rdb from a directory of files\n"
       << "  --unpack: extracts files from an .rdb into a directory\n";
}

static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t read_u64(const uint8_t* p) {
    return static_cast<uint64_t>(read_u32(p)) | (static_cast<uint64_t>(read_u32(p + 4)) << 32);
}

static void write_le(std::ostream& out, uint64_t value, int bytes) {
    char buf[8];
    for (int i = 0; i < bytes; i++) {
        buf[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    out.write(buf, bytes);
}

static bool read_file(const fs::path& path, std::vector<uint8_t>& data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    data.resize(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(in);
}

bool unpack(const fs::path& input, const fs::path& output_dir) {
    std::vector<uint8_t> data;
    if (!read_file(input, data)) {
        std::cerr << "cannot read input file: " << input << "\n";
        return false;
    }

    if (data.size() < RDB_MIN_SIZE || std::memcmp(data.data(), RDB_MAGIC.data(), RDB_MAGIC.size()) != 0) {
        std::cerr << "invalid rdb file\n";
        return false;
    }

    const uint32_t file_num = read_u32(data.data() + 0x10);
    const uint64_t index_offset = read_u64(data.data() + 0x14);
    const uint64_t index_size = read_u64(data.data() + 0x1C);
    if (index_offset > data.size() || index_size > data.size() - index_offset) {
        std::cerr << "index is outside the file\n";
        return false;
    }
    const uint64_t content_offset = index_offset + index_size;

    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        std::cerr << "cannot create output directory " << output_dir << ": " << ec.message() << "\n";
        return false;
    }

    size_t p_index = static_cast<size_t>(index_offset);
    uint32_t extracted = 0;
    uint32_t directories = 0;
    bool ok = true;

    for (uint32_t i = 0; i < file_num; i++) {
        // skip the utf-16 name, it must be terminated inside the index
        while (p_index + 2 <= content_offset && (data[p_index] | data[p_index + 1]) != 0) {
            p_index += 2;
        }
        p_index += 2;
        if (p_index + 16 > content_offset) {
            std::cerr << "index truncated at entry " << i << " of " << file_num << "\n";
            ok = false;
            break;
        }

        const uint64_t offset = read_u64(data.data() + p_index);
        const uint64_t size = read_u64(data.data() + p_index + 8);
        p_index += 16;

        // size 0 marks a directory entry
        if (size == 0) {
            directories++;
            continue;
        }

        if (offset > data.size() || size > data.size() - offset) {
            std::cerr << "entry " << i << " points outside the file\n";
            ok = false;
            continue;
        }

        const std::string filename = "file_" + std::to_string(i) + ".bin"; // simplified naming
        std::ofstream out(output_dir / filename, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data() + offset), static_cast<std::streamsize>(size));
        out.close();
        if (!out) {
            std::cerr << "cannot write " << (output_dir / filename) << "\n";
            ok = false;
            continue;
        }
        extracted++;
    }

    std::cout << "unpacked " << extracted << " of " << (file_num - directories) << " files to " << output_dir;
    if (directories > 0) {
        std::cout << " (" << directories << " directory entries skipped)";
    }
    std::cout << "\n";
    return ok;
}

bool pack(const fs::path& input_dir, const fs::path& output) {
    std::error_code ec;
    if (!fs::is_directory(input_dir, ec)) {
        std::cerr << "input is not a directory: " << input_dir << "\n";
        return false;
    }

    std::vector<RdbFileEntry> entries;
    for (const auto& entry : fs::directory_iterator(input_dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (fs::exists(output, ec) && fs::equivalent(entry.path(), output, ec)) {
            continue;
        }
        RdbFileEntry rdb_entry;
        rdb_entry.name = entry.path().filename().u16string();
        rdb_entry.name += static_cast<char16_t>(0);
        if (!read_file(entry.path(), rdb_entry.data)) {
            std::cerr << "cannot read " << entry.path() << "\n";
            return false;
        }
        entries.push_back(std::move(rdb_entry));
    }
    if (ec) {
        std::cerr << "cannot list " << input_dir << ": " << ec.message() << "\n";
        return false;
    }

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output file: " << output << "\n";
        return false;
    }

    // header
    out.write(reinterpret_cast<const char*>(RDB_MAGIC.data()), RDB_MAGIC.size());
    const uint32_t file_num = static_cast<uint32_t>(entries.size());
    write_le(out, file_num, 4);

    const uint64_t index_offset = RDB_MIN_SIZE;
    write_le(out, index_offset, 8);

    uint64_t index_size = 0;
    for (const auto& e : entries) {
        index_size += (e.name.size() * 2) + 16;
    }
    write_le(out, index_size, 8);

    // write index (with correct offsets)
    uint64_t current_payload_offset = index_offset + index_size;
    for (const auto& e : entries) {
        for (char16_t c : e.name) {
            write_le(out, c, 2);
        }
        write_le(out, current_payload_offset, 8);
        const uint64_t size = e.data.size();
        write_le(out, size, 8);
        current_payload_offset += size;
    }

    // write payloads
    for (const auto& e : entries) {
        out.write(reinterpret_cast<const char*>(e.data.data()), static_cast<std::streamsize>(e.data.size()));
    }

    out.close();
    if (!out) {
        std::cerr << "write error on " << output << "\n";
        return false;
    }
    std::cout << "packed " << file_num << " files into " << output << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "--version") {
            std::cout << RDBCPP_VERSION << "\n";
            return EXIT_SUCCESS;
        }
        if (arg == "-h" || arg == "--help") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }
    }

    if (argc != 4) {
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    const std::string command = argv[1];
    const fs::path input = argv[2];
    const fs::path output = argv[3];
    bool success = false;
    try {
        if (command == "--pack") {
            success = pack(input, output);
        } else if (command == "--unpack") {
            success = unpack(input, output);
        } else {
            print_usage(std::cerr);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
    }
    
    if (success) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
