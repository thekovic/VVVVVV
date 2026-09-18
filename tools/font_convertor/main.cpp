#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Glyph
{
    std::uint32_t id;
    std::uint32_t image_index;
    unsigned advance;
};

struct FontMetadata
{
    std::string display_name;
    unsigned width = 0;
    unsigned height = 0;
    std::vector<Glyph> glyphs;
};

struct AtlasSize
{
    unsigned width;
    unsigned height;
};

static void print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0 << " [options] <fontmeta-file>\n"
              << "  --output <file>       Output BMFont .fnt file (default: beside input)\n"
              << "  --page <file>         Texture filename in the BMFont descriptor\n"
              << "  --columns <count>     Atlas columns when the PNG is unavailable (default: 16)\n"
              << "  --help                Show this help message\n";
}

static std::string read_file(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Unable to open " + path.string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static std::string element_text(const std::string& xml, const std::string& name)
{
    const std::string start_tag = "<" + name + ">";
    const std::string end_tag = "</" + name + ">";
    const std::size_t start = xml.find(start_tag);
    if (start == std::string::npos)
    {
        return {};
    }
    const std::size_t content_start = start + start_tag.size();
    const std::size_t end = xml.find(end_tag, content_start);
    if (end == std::string::npos)
    {
        return {};
    }
    return xml.substr(content_start, end - content_start);
}

static unsigned parse_number(const std::string& value, const std::string& field)
{
    try
    {
        std::size_t parsed = 0;
        const unsigned long number = std::stoul(value, &parsed, 0);
        if (parsed != value.size() || number > 0x10FFFF)
        {
            throw std::invalid_argument("out of range");
        }
        return static_cast<unsigned>(number);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("Invalid " + field + " value: " + value);
    }
}

static std::string attribute(const std::string& attributes, const std::string& name)
{
    const std::regex expression(name + "\\s*=\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(attributes, match, expression))
    {
        return {};
    }
    return match[1].str();
}

static FontMetadata parse_metadata(const fs::path& path)
{
    const std::string xml = read_file(path);
    FontMetadata metadata;
    metadata.display_name = element_text(xml, "display_name");
    metadata.width = parse_number(element_text(xml, "width"), "width");
    metadata.height = parse_number(element_text(xml, "height"), "height");
    if (metadata.width == 0 || metadata.height == 0)
    {
        throw std::runtime_error("Font width and height must be positive");
    }

    const std::regex range_expression("<range([^>]*)>");
    const std::size_t chars_start = xml.find("<chars>");
    const std::size_t chars_end = xml.find("</chars>", chars_start);
    if (chars_start == std::string::npos || chars_end == std::string::npos)
    {
        throw std::runtime_error("Missing <chars> element");
    }

    std::set<std::uint32_t> seen;
    const std::string chars_xml = xml.substr(chars_start + 7, chars_end - chars_start - 7);
    auto range_begin = std::sregex_iterator(chars_xml.begin(), chars_xml.end(), range_expression);
    const auto range_end = std::sregex_iterator();
    for (auto current = range_begin; current != range_end; ++current)
    {
        const std::string start_text = attribute((*current)[1].str(), "start");
        const std::string end_text = attribute((*current)[1].str(), "end");
        if (start_text.empty() || end_text.empty())
        {
            throw std::runtime_error("A character range must have start and end attributes");
        }

        const unsigned start = parse_number(start_text, "range start");
        const unsigned end = parse_number(end_text, "range end");
        if (end < start)
        {
            throw std::runtime_error("Character range end precedes its start");
        }

        for (unsigned codepoint = start; codepoint <= end; ++codepoint)
        {
            if (seen.insert(codepoint).second)
            {
                metadata.glyphs.push_back({codepoint, static_cast<unsigned>(metadata.glyphs.size()), metadata.width});
            }
            if (codepoint == 0x10FFFF)
            {
                break;
            }
        }
    }

    const std::size_t special_start = xml.find("<special>");
    const std::size_t special_end = xml.find("</special>", special_start);
    if (special_start != std::string::npos && special_end != std::string::npos)
    {
        const std::string special_xml = xml.substr(special_start + 9, special_end - special_start - 9);
        auto special_begin = std::sregex_iterator(special_xml.begin(), special_xml.end(), range_expression);
        for (auto current = special_begin; current != range_end; ++current)
        {
            const std::string start_text = attribute((*current)[1].str(), "start");
            const std::string end_text = attribute((*current)[1].str(), "end");
            const std::string advance_text = attribute((*current)[1].str(), "advance");
            if (start_text.empty() || end_text.empty() || advance_text.empty())
            {
                continue;
            }

            const unsigned start = parse_number(start_text, "special range start");
            const unsigned end = parse_number(end_text, "special range end");
            const unsigned advance = parse_number(advance_text, "advance");
            if (advance > 255)
            {
                throw std::runtime_error("Advance must fit in an unsigned byte");
            }
            for (Glyph& glyph : metadata.glyphs)
            {
                if (glyph.id >= start && glyph.id <= end)
                {
                    glyph.advance = advance;
                }
            }
        }
    }

    return metadata;
}

static AtlasSize read_png_size(const fs::path& path)
{
    const std::string bytes = read_file(path);
    if (bytes.size() < 24 || static_cast<unsigned char>(bytes[0]) != 0x89
        || bytes.substr(1, 3) != "PNG" || bytes.substr(12, 4) != "IHDR")
    {
        throw std::runtime_error("Not a PNG file: " + path.string());
    }

    const auto read_uint32 = [&bytes](std::size_t offset) {
        return (static_cast<unsigned char>(bytes[offset]) << 24)
            | (static_cast<unsigned char>(bytes[offset + 1]) << 16)
            | (static_cast<unsigned char>(bytes[offset + 2]) << 8)
            | static_cast<unsigned char>(bytes[offset + 3]);
    };
    return {static_cast<unsigned>(read_uint32(16)), static_cast<unsigned>(read_uint32(20))};
}

static std::string quote(const std::string& value)
{
    std::string result;
    for (const char character : value)
    {
        if (character == '\\' || character == '"')
        {
            result += '\\';
        }
        result += character;
    }
    return result;
}

static void write_descriptor(const fs::path& output_path, const FontMetadata& metadata,
    const std::string& page_name, const AtlasSize& atlas)
{
    std::ofstream output(output_path, std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("Unable to write " + output_path.string());
    }

    output << "info face=\"" << quote(metadata.display_name) << "\" size=" << metadata.height
           << " bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=0 aa=1"
              " padding=0,0,0,0 spacing=0,0 outline=0\n";
    output << "common lineHeight=" << metadata.height << " base=" << metadata.height
           << " scaleW=" << atlas.width << " scaleH=" << atlas.height
            << " pages=1 packed=0 alphaChnl=0 redChnl=0 greenChnl=0 blueChnl=0\n";
    output << "page id=0 file=\"" << quote(page_name) << "\"\n";
    output << "chars count=" << metadata.glyphs.size() << "\n";
    const unsigned columns = atlas.width / metadata.width;
    for (const Glyph& glyph : metadata.glyphs)
    {
        const unsigned x = (glyph.image_index % columns) * metadata.width;
        const unsigned y = (glyph.image_index / columns) * metadata.height;
        output << "char id=" << glyph.id << " x=" << x << " y=" << y
               << " width=" << metadata.width << " height=" << metadata.height
               << " xoffset=0 yoffset=0 xadvance=" << glyph.advance
               << " page=0 chnl=15\n";
    }
}

int main(int argc, char** argv)
{
    try
    {
        fs::path input_path;
        fs::path output_path;
        std::string page_name;
        unsigned columns = 16;

        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--help" || argument == "-h")
            {
                print_usage(argv[0]);
                return 0;
            }
            if (argument == "--output" || argument == "--page" || argument == "--columns")
            {
                if (index + 1 >= argc)
                {
                    throw std::runtime_error("Missing value for " + argument);
                }
                const std::string value = argv[++index];
                if (argument == "--output")
                {
                    output_path = value;
                }
                else if (argument == "--page")
                {
                    page_name = value;
                }
                else
                {
                    columns = parse_number(value, "columns");
                    if (columns == 0)
                    {
                        throw std::runtime_error("Columns must be positive");
                    }
                }
                continue;
            }
            if (!input_path.empty())
            {
                throw std::runtime_error("Unexpected argument: " + argument);
            }
            input_path = argument;
        }

        if (input_path.empty())
        {
            print_usage(argv[0]);
            return 1;
        }

        const FontMetadata metadata = parse_metadata(input_path);
        const fs::path default_page_path = input_path.parent_path() / (input_path.stem().string() + ".png");
        if (page_name.empty())
        {
            page_name = default_page_path.filename().string();
        }
        if (output_path.empty())
        {
            output_path = input_path.parent_path() / (input_path.stem().string() + ".fnt");
        }

        AtlasSize atlas{metadata.width * columns, metadata.height
            * ((static_cast<unsigned>(metadata.glyphs.size()) + columns - 1) / columns)};
        if (fs::exists(default_page_path))
        {
            atlas = read_png_size(default_page_path);
            if (atlas.width == 0 || atlas.height == 0 || atlas.width % metadata.width != 0
                || atlas.height % metadata.height != 0)
            {
                throw std::runtime_error("PNG dimensions are not divisible by glyph dimensions");
            }
        }
        if (atlas.width / metadata.width == 0)
        {
            throw std::runtime_error("Atlas has no glyph columns");
        }

        write_descriptor(output_path, metadata, page_name, atlas);
        std::cout << "Wrote " << metadata.glyphs.size() << " glyphs to " << output_path << "\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}