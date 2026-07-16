#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct resourceheader
{
    char name[48];
    std::int32_t start_UNUSED;
    std::int32_t size;
    std::uint8_t valid;
};

static void print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0 << " [options] [blob-file]"
              << "\n"
              << "  --output <dir>          Directory for extracted files (default: data/music)\n"
              << "  --convert-to-mp3        Convert extracted OGG files to MP3 with ffmpeg\n"
              << "  --help                  Show this help message\n";
}

static std::string sanitize_name(const std::string& original_name)
{
    std::string cleaned = original_name;
    if (cleaned.rfind("data/", 0) == 0)
    {
        cleaned.erase(0, 5);
    }

    const auto slash = cleaned.find('/');
    if (slash != std::string::npos)
    {
        cleaned = cleaned.substr(slash + 1);
    }

    const auto backslash = cleaned.find('\\');
    if (backslash != std::string::npos)
    {
        cleaned = cleaned.substr(backslash + 1);
    }

    return cleaned;
}

static bool has_ogg_extension(const std::string& name)
{
    return name.size() >= 4 && name.compare(name.size() - 4, 4, ".ogg") == 0;
}

static bool is_music_entry(const std::string& name)
{
    return name.find("music/") != std::string::npos && has_ogg_extension(name);
}

static int run_ffmpeg_conversion(const fs::path& source_path, const fs::path& output_dir)
{
    const fs::path mp3_path = output_dir / (source_path.stem().string() + ".mp3");
    const std::string command =
        "ffmpeg -y -i \"" + source_path.string() + "\" -vn -acodec libmp3lame \"" + mp3_path.string() + "\"";

    const int exit_code = std::system(command.c_str());
    if (exit_code != 0)
    {
        std::cerr << "ffmpeg conversion failed for " << source_path << "\n";
        return exit_code;
    }

    std::filesystem::remove(source_path);
    return 0;
}

int main(int argc, char** argv)
{
    std::string blob_path;
    std::string output_dir = "data/music";
    bool convert_to_mp3 = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--convert-to-mp3")
        {
            convert_to_mp3 = true;
        }
        else if (arg == "--output")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for --output\n";
                return 1;
            }
            output_dir = argv[++i];
        }
        else if (blob_path.empty())
        {
            blob_path = arg;
        }
        else
        {
            std::cerr << "Unexpected argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (blob_path.empty())
    {
        blob_path = "data/vvvvvmusic.vvv";
    }

    if (!fs::exists(blob_path))
    {
        std::cerr << "Blob file not found: " << blob_path << "\n";
        return 1;
    }

    fs::create_directories(output_dir);

    std::ifstream input(blob_path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        std::cerr << "Unable to open blob file: " << blob_path << "\n";
        return 1;
    }

    const std::streamsize file_size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<size_t>(file_size));
    input.read(reinterpret_cast<char*>(bytes.data()), file_size);

    static constexpr std::size_t kMaxHeaders = 128;
    std::array<resourceheader, kMaxHeaders> headers{};
    const std::size_t header_bytes = std::min<std::size_t>(sizeof(headers), bytes.size());
    std::memcpy(headers.data(), bytes.data(), header_bytes);

    std::size_t payload_offset = header_bytes;
    std::size_t extracted_count = 0;

    for (const auto& header : headers)
    {
        std::string name(header.name, sizeof(header.name));
        const auto null_pos = name.find('\0');
        if (null_pos != std::string::npos)
        {
            name.resize(null_pos);
        }

        if (!header.valid || name.empty())
        {
            if (header.size > 0)
            {
                payload_offset += static_cast<std::size_t>(header.size);
            }
            continue;
        }

        if (!is_music_entry(name))
        {
            if (header.size > 0)
            {
                payload_offset += static_cast<std::size_t>(header.size);
            }
            continue;
        }

        const std::size_t payload_size = static_cast<std::size_t>(header.size);
        if (payload_offset + payload_size > bytes.size())
        {
            std::cerr << "Skipping truncated payload for: " << name << "\n";
            payload_offset += payload_size;
            continue;
        }

        const fs::path output_path = fs::path(output_dir) / sanitize_name(name);
        fs::create_directories(output_path.parent_path());

        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            std::cerr << "Unable to write output file: " << output_path << "\n";
            payload_offset += payload_size;
            continue;
        }

        output.write(reinterpret_cast<const char*>(bytes.data() + payload_offset), static_cast<std::streamsize>(payload_size));
        output.close();

        if (convert_to_mp3)
        {
            if (run_ffmpeg_conversion(output_path, fs::path(output_dir)) != 0)
            {
                payload_offset += payload_size;
                continue;
            }
        }

        ++extracted_count;
        payload_offset += payload_size;
    }

    std::cout << "Extracted " << extracted_count << " music payload(s) into " << output_dir << "\n";
    return 0;
}
