#include "TerrainMetadata.hpp"

#include "ImageWriter.hpp" // pathFromUtf8

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace prs
{
    namespace
    {
        constexpr const char* kSuffix = ".terrain.json";

        bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

        // Pozicija prvog znaka vrijednosti iza `"key":`; npos ako ključa nema.
        std::size_t findValue(const std::string& s, const std::string& key)
        {
            const std::string needle = "\"" + key + "\"";
            std::size_t pos = 0;
            while ((pos = s.find(needle, pos)) != std::string::npos)
            {
                std::size_t i = pos + needle.size();
                while (i < s.size() && isSpace(s[i]))
                    ++i;
                if (i < s.size() && s[i] == ':')
                {
                    ++i;
                    while (i < s.size() && isSpace(s[i]))
                        ++i;
                    return i;
                }
                pos += needle.size(); // string s tim sadržajem, a nije ključ – traži dalje
            }
            return std::string::npos;
        }

        std::optional<double> readNumber(const std::string& s, const std::string& key)
        {
            const std::size_t i = findValue(s, key);
            if (i == std::string::npos)
                return std::nullopt;
            double v = 0.0;
            const auto [ptr, ec] = std::from_chars(s.data() + i, s.data() + s.size(), v, std::chars_format::general);
            if (ec != std::errc{} || !std::isfinite(v))
                throw std::runtime_error("Metapodaci terena: vrijednost \"" + key + "\" nije ispravan broj.");
            return v;
        }

        std::string readString(const std::string& s, const std::string& key)
        {
            std::size_t i = findValue(s, key);
            if (i == std::string::npos || i >= s.size() || s[i] != '"')
                return {};
            std::string out;
            for (++i; i < s.size() && s[i] != '"'; ++i)
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    ++i;
                    switch (s[i])
                    {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    default: out += s[i]; break; // \" \\ \/ i ostalo
                    }
                }
                else
                    out += s[i];
            }
            return out;
        }

        bool endsWith(const std::string& text, const std::string& suffix)
        {
            return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
    }

    TerrainMetadata parseTerrainMetadata(const std::string& text)
    {
        TerrainMetadata m;
        if (const auto version = readNumber(text, "formatVersion"); version && *version > 1.0)
            throw std::runtime_error("Metapodaci terena su novije verzije (" + std::to_string(static_cast<int>(*version)) +
                                     ") nego što ovaj program razumije.");

        const auto cell = readNumber(text, "cellSizeMeters");
        const auto lo = readNumber(text, "minElevationMeters");
        const auto hi = readNumber(text, "maxElevationMeters");
        if (!cell)
            throw std::runtime_error("Metapodaci terena: nedostaje \"cellSizeMeters\".");
        if (!lo)
            throw std::runtime_error("Metapodaci terena: nedostaje \"minElevationMeters\".");
        if (!hi)
            throw std::runtime_error("Metapodaci terena: nedostaje \"maxElevationMeters\".");
        m.scale.cellSizeMeters = *cell;
        m.scale.minElevationMeters = *lo;
        m.scale.maxElevationMeters = *hi;
        if (const std::string problem = m.scale.validate(); !problem.empty())
            throw std::runtime_error("Metapodaci terena nisu ispravni: " + problem);

        if (const auto w = readNumber(text, "width"))
            m.width = static_cast<int>(std::lround(*w));
        if (const auto h = readNumber(text, "height"))
            m.height = static_cast<int>(std::lround(*h));
        m.source = readString(text, "source");
        m.heightmapFile = readString(text, "heightmapFile");

        const auto s = readNumber(text, "south"), w = readNumber(text, "west"), n = readNumber(text, "north"),
                   e = readNumber(text, "east");
        if (s && w && n && e)
        {
            m.hasBounds = true;
            m.south = *s;
            m.west = *w;
            m.north = *n;
            m.east = *e;
        }
        return m;
    }

    TerrainMetadata loadTerrainMetadata(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            throw std::runtime_error("Ne mogu otvoriti metapodatke terena: " + pathToUtf8(file));
        std::ostringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
            text.erase(0, 3); // UTF-8 BOM
        TerrainMetadata m = parseTerrainMetadata(text);
        m.jsonPath = pathToUtf8(file);
        return m;
    }

    std::filesystem::path terrainMetadataSidecar(const std::filesystem::path& image)
    {
        std::filesystem::path out = image;
        out.replace_extension(); // skida samo posljednji nastavak (.png)
        out += kSuffix;
        return out;
    }

    std::string adaptMetadataToImage(TerrainMetadata& m, int imageWidth, int imageHeight, bool* rescaled)
    {
        if (rescaled)
            *rescaled = false;
        if (m.width <= 0 || m.height <= 0 || imageWidth <= 0 || imageHeight <= 0)
            return {}; // dimenzije u JSON-u nisu poznate – ne može se provjeriti
        if (m.width == imageWidth && m.height == imageHeight)
            return {};
        const double fx = static_cast<double>(m.width) / imageWidth;
        const double fy = static_cast<double>(m.height) / imageHeight;
        if (std::abs(fx - fy) / std::max(fx, fy) > 0.02)
            return "slika ima dimenzije " + std::to_string(imageWidth) + " x " + std::to_string(imageHeight) +
                   ", a metapodaci su za " + std::to_string(m.width) + " x " + std::to_string(m.height) +
                   " (promijenjen omjer stranica, razmjera se ne može preračunati)";
        m.scale.cellSizeMeters *= 0.5 * (fx + fy); // manja slika => veća ćelija za isto područje
        m.width = imageWidth;
        m.height = imageHeight;
        if (rescaled)
            *rescaled = true;
        return {};
    }

    std::optional<TerrainExport> findLatestTerrainExport(const std::filesystem::path& folder)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_directory(folder, ec))
            return std::nullopt;
        std::optional<TerrainExport> best;
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec))
        {
            std::error_code e2;
            if (!it->is_regular_file(e2))
                continue;
            const std::string name = pathToUtf8(it->path().filename());
            if (!endsWith(name, kSuffix))
                continue;
            fs::path png = it->path();
            png.replace_filename(pathFromUtf8(name.substr(0, name.size() - std::string(kSuffix).size()) + ".png"));
            if (!fs::is_regular_file(png, e2))
                continue;
            const auto time = fs::last_write_time(it->path(), e2);
            if (e2)
                continue;
            if (!best || time > best->modified)
                best = TerrainExport{it->path(), png, time};
        }
        return best;
    }
}
