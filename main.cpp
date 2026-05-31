#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <array>

namespace fs = std::filesystem;

// tencent rdb header magic: 531E98204F8542F0
constexpr std::array<uint8_t, 16> RDB_MAGIC = { 0x35, 0x33, 0x31, 0x45, 0x39, 0x38, 0x32, 0x30, 0x34, 0x46, 0x38, 0x35, 0x34, 0x32, 0x46, 0x30 };
constexpr uint64_t RDB_MIN_SIZE = 0x20;

struct RdbFileEntry {
    std::u16string name;
    std::vector<uint8_t> data;
};

void print_usage() {
    std::cout << "usage: rdbcpp <pack|unpack> <input_path> <output_path>\n"
              << "  --pack:   creates an .rdb from a directory of files\n"
              << "  --unpack: extracts files from an .rdb into a directory\n";
}

bool unpack(const fs::path& input, const fs::path& output_dir) {
    std::ifstream in(input, std::ios::binary);
    if (!in) {
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (data.size() < RDB_MIN_SIZE || std::memcmp(data.data(), RDB_MAGIC.data(), RDB_MAGIC.size()) != 0) {
        std::cerr << "invalid rdb file\n";
        return false;
    }

    uint32_t file_num = *reinterpret_cast<uint32_t*>(data.data() + 0x10);
    uint64_t index_offset = *reinterpret_cast<uint64_t*>(data.data() + 0x14);
    uint64_t index_size = *reinterpret_cast<uint64_t*>(data.data() + 0x1C);
    uint64_t content_offset = index_offset + index_size;

    fs::create_directories(output_dir);
    size_t p_index = index_offset;

    for (uint32_t i = 0; i < file_num; i++) {
        const char16_t* name_ptr = reinterpret_cast<const char16_t*>(data.data() + p_index);
        size_t name_len = 0;
        while ((p_index + ((name_len + 1) * 2)) <= content_offset && name_ptr[name_len] != 0) {
            name_len++;
        }
        
        p_index += ((name_len + 1) * 2);
        if ((p_index + 16) > content_offset) {
            break;
        }

        uint64_t offset = *reinterpret_cast<uint64_t*>(data.data() + p_index);
        uint64_t size = *reinterpret_cast<uint64_t*>(data.data() + p_index + 8);
        p_index += 16;

        if (size > 0 && (offset + size) <= data.size()) {
            std::string filename = "file_" + std::to_string(i) + ".bin"; // simplified naming
            std::ofstream out(output_dir / filename, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data() + offset), size);
            out.close();
        }
    }

    std::cout << "unpacked " << file_num << " files to " << output_dir << "\n";
    return true;
}

bool pack(const fs::path& input_dir, const fs::path& output) {
    std::vector<RdbFileEntry> entries;
    for (const auto& entry : fs::directory_iterator(input_dir)) {
        if (entry.is_regular_file()) {
            std::ifstream in(entry.path(), std::ios::binary);
            RdbFileEntry rdb_entry;
            std::string name = entry.path().filename().string();
            for(char c : name) {
                rdb_entry.name += static_cast<char16_t>(c);
            }
            rdb_entry.name += static_cast<char16_t>(0);
            rdb_entry.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            entries.push_back(rdb_entry);
        }
    }

    std::ofstream out(output, std::ios::binary);
    
    // header
    out.write(reinterpret_cast<const char*>(RDB_MAGIC.data()), RDB_MAGIC.size());
    uint32_t file_num = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&file_num), 4);
    
    uint64_t index_offset = RDB_MIN_SIZE; 
    out.write(reinterpret_cast<const char*>(&index_offset), 8);
    
    // calculate index size
    uint64_t index_size = 0;
    for (const auto& e : entries) {
        index_size += (e.name.size() * 2) + 16;
    }
    out.write(reinterpret_cast<const char*>(&index_size), 8);

    // write index (with correct offsets)
    uint64_t current_payload_offset = index_offset + index_size;
    for (const auto& e : entries) {
        out.write(reinterpret_cast<const char*>(e.name.data()), e.name.size() * 2);
        out.write(reinterpret_cast<const char*>(&current_payload_offset), 8);
        uint64_t size = e.data.size();
        out.write(reinterpret_cast<const char*>(&size), 8);
        current_payload_offset += size;
    }

    // write payloads
    for (const auto& e : entries) {
        out.write(reinterpret_cast<const char*>(e.data.data()), e.data.size());
    }

    out.close();
    std::cout << "packed " << file_num << " files into " << output << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];
    const fs::path input = argv[2];
    const fs::path output = argv[3];
    bool success;
    if (command == "--pack") {
        success = pack(input, output);;
    } else if (command == "--unpack") {
        success = unpack(input, output);
    } else {
        success = false;
        print_usage();
    }
    
    if (success) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
