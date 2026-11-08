#include "ResultExport.hpp"

#include "ImageWriter.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace prs
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string joinPath(const std::string& dir, const std::string& file)
        {
            return pathToUtf8(pathFromUtf8(dir) / pathFromUtf8(file));
        }

        std::string localTimestamp()
        {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm tmLocal{};
#if defined(_WIN32)
            localtime_s(&tmLocal, &now);
#else
            localtime_r(&now, &tmLocal);
#endif
            std::ostringstream os;
            os << std::put_time(&tmLocal, "%Y-%m-%d %H:%M:%S");
            return os.str();
        }

        double percentile(std::vector<double> values, double p)
        {
            if (values.empty())
                return 0.0;
            const std::size_t k = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
            std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
            return values[k];
        }

        // Linearna interpolacija boje.
        void mix(std::uint8_t* out, const double a[3], const double b[3], double t)
        {
            for (int c = 0; c < 3; ++c)
                out[c] = static_cast<std::uint8_t>(std::lround(a[c] + (b[c] - a[c]) * std::clamp(t, 0.0, 1.0)));
        }

        // Gradijent visine (centralne razlike, jednostrane na rubu) – koristi se za hillshade i z-faktor.
        void gradientAt(const Grid2D<double>& h, int x, int y, double cell, double& gx, double& gy)
        {
            const int W = h.width(), H = h.height();
            const int x0 = std::max(0, x - 1), x1 = std::min(W - 1, x + 1);
            const int y0 = std::max(0, y - 1), y1 = std::min(H - 1, y + 1);
            gx = (x1 > x0) ? (h(x1, y) - h(x0, y)) / ((x1 - x0) * cell) : 0.0;
            gy = (y1 > y0) ? (h(x, y1) - h(x, y0)) / ((y1 - y0) * cell) : 0.0;
        }

        // ------------------------------------------------------------------ JSON (minimalni pisač)
        std::string jsonEscape(const std::string& s)
        {
            std::ostringstream os;
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"': os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\n': os << "\\n"; break;
                case '\r': os << "\\r"; break;
                case '\t': os << "\\t"; break;
                default:
                    if (c < 0x20)
                        os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                    else
                        os << c;
                }
            }
            return os.str();
        }

        std::string jsonNumber(double v)
        {
            if (!std::isfinite(v))
                return "null";
            // Najkraći zapis koji se tačno vraća u isti double (npr. 0.1, a ne 0.10000000000000001).
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
            return std::string(buffer, result.ptr);
        }

        class JsonObject
        {
        public:
            JsonObject& num(const std::string& k, double v) { return raw(k, jsonNumber(v)); }
            JsonObject& integer(const std::string& k, std::int64_t v) { return raw(k, std::to_string(v)); }
            JsonObject& str(const std::string& k, const std::string& v) { return raw(k, "\"" + jsonEscape(v) + "\""); }
            JsonObject& boolean(const std::string& k, bool v) { return raw(k, v ? "true" : "false"); }
            // Ugniježđeni objekat se formatira s uvlakom 2 (podržan je jedan nivo ugnježđivanja).
            JsonObject& object(const std::string& k, const JsonObject& o) { return raw(k, o.text(2)); }
            JsonObject& strings(const std::string& k, const std::vector<std::string>& v)
            {
                std::string a = "[";
                for (std::size_t i = 0; i < v.size(); ++i)
                    a += (i ? ", \"" : "\"") + jsonEscape(v[i]) + "\"";
                return raw(k, a + "]");
            }
            std::string text(int indent = 0) const
            {
                const std::string pad(static_cast<std::size_t>(indent) + 2, ' ');
                std::string out = "{\n";
                for (std::size_t i = 0; i < _items.size(); ++i)
                    out += pad + "\"" + jsonEscape(_items[i].first) + "\": " + _items[i].second +
                           (i + 1 < _items.size() ? ",\n" : "\n");
                return out + std::string(static_cast<std::size_t>(indent), ' ') + "}";
            }

        private:
            JsonObject& raw(const std::string& k, const std::string& v)
            {
                _items.emplace_back(k, v);
                return *this;
            }
            std::vector<std::pair<std::string, std::string>> _items;
        };
    }

    // ---------------------------------------------------------------------- .npy
    void saveNpy(const std::string& path, const Grid2D<double>& grid)
    {
        std::string header = "{'descr': '<f8', 'fortran_order': False, 'shape': (" + std::to_string(grid.height()) +
                             ", " + std::to_string(grid.width()) + "), }";
        const std::size_t base = 10 + header.size() + 1; // magic(6) + verzija(2) + dužina(2) + zaglavlje + '\n'
        header.append((64 - base % 64) % 64, ' ');
        header.push_back('\n');

        std::ofstream out(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Ne mogu otvoriti " + path);
        out.write("\x93NUMPY", 6);
        const char version[2] = {1, 0};
        out.write(version, 2);
        const auto len = static_cast<std::uint16_t>(header.size());
        const char lenBytes[2] = {static_cast<char>(len & 0xFF), static_cast<char>(len >> 8)};
        out.write(lenBytes, 2);
        out.write(header.data(), static_cast<std::streamsize>(header.size()));
        // Little-endian je pretpostavljen (x86/x64 i ARM u little-endian modu).
        out.write(reinterpret_cast<const char*>(grid.data()), static_cast<std::streamsize>(grid.size() * sizeof(double)));
        if (!out)
            throw std::runtime_error("Greska pri pisanju " + path);
    }

    // ---------------------------------------------------------------------- vizualizacije
    std::vector<std::uint8_t> renderDifference(const Grid2D<double>& difference, double colorScale)
    {
        // Divergentna skala: crveno-smeđe = sniženje (erozija), bijelo = bez promjene, plavo = povišenje (depozicija).
        static const double eroded[3] = {165.0, 0.0, 38.0};
        static const double neutral[3] = {247.0, 247.0, 247.0};
        static const double deposited[3] = {49.0, 54.0, 149.0};
        std::vector<std::uint8_t> rgb(difference.size() * 3);
        for (std::size_t i = 0; i < difference.size(); ++i)
        {
            const double d = difference[i];
            const double t = colorScale > 0.0 ? std::min(std::abs(d) / colorScale, 1.0) : 0.0;
            mix(&rgb[3 * i], neutral, d < 0.0 ? eroded : deposited, t);
        }
        return rgb;
    }

    std::vector<std::uint8_t> renderHillshade(const Grid2D<double>& elevation, double cellSize, double zFactor)
    {
        // Lambertovo sjenčenje, svjetlo sa sjeverozapada (gore lijevo na slici), visina sunca 45°.
        const double altitude = 45.0 * 3.14159265358979323846 / 180.0;
        const double lx = -std::cos(altitude) / std::sqrt(2.0);
        const double ly = -std::cos(altitude) / std::sqrt(2.0);
        const double lz = std::sin(altitude);
        std::vector<std::uint8_t> rgb(elevation.size() * 3);
        for (int y = 0; y < elevation.height(); ++y)
        {
            for (int x = 0; x < elevation.width(); ++x)
            {
                double gx, gy;
                gradientAt(elevation, x, y, cellSize, gx, gy);
                const double nx = -zFactor * gx, ny = -zFactor * gy, nz = 1.0;
                const double shade = std::max(0.0, (nx * lx + ny * ly + nz * lz) / std::sqrt(nx * nx + ny * ny + nz * nz));
                const auto v = static_cast<std::uint8_t>(std::lround(30.0 + 225.0 * shade));
                const std::size_t i = elevation.index(x, y) * 3;
                rgb[i] = rgb[i + 1] = rgb[i + 2] = v;
            }
        }
        return rgb;
    }

    std::vector<std::uint8_t> renderWaterDepth(const Grid2D<double>& depth)
    {
        // Logaritamska skala od 1 mm do najveće dubine; < 1 mm = bijelo.
        static const double dry[3] = {255.0, 255.0, 255.0};
        static const double shallow[3] = {198.0, 219.0, 239.0};
        static const double deep[3] = {8.0, 48.0, 107.0};
        double maxDepth = 0.0;
        for (double d : depth.values())
            maxDepth = std::max(maxDepth, d);
        const double lo = std::log10(1.0e-3);
        const double hi = std::log10(std::max(maxDepth, 2.0e-3));
        std::vector<std::uint8_t> rgb(depth.size() * 3);
        for (std::size_t i = 0; i < depth.size(); ++i)
        {
            if (depth[i] < 1.0e-3)
                mix(&rgb[3 * i], dry, dry, 0.0);
            else
                mix(&rgb[3 * i], shallow, deep, (std::log10(depth[i]) - lo) / (hi - lo));
        }
        return rgb;
    }

    // ---------------------------------------------------------------------- izvještaj
    namespace
    {
        std::string fmt(double v, int precision = 4)
        {
            std::ostringstream os;
            os << std::setprecision(precision) << v;
            return os.str();
        }

        std::string fixed(double v, int decimals)
        {
            std::ostringstream os;
            os << std::fixed << std::setprecision(decimals) << v;
            return os.str();
        }

        std::string sci(double v)
        {
            std::ostringstream os;
            os << std::scientific << std::setprecision(3) << v;
            return os.str();
        }

        std::string duration(double seconds)
        {
            std::ostringstream os;
            os << fixed(seconds, 1) << " s (" << fixed(seconds / 3600.0, 3) << " h)";
            return os.str();
        }

        void writeTextReport(const std::string& path, const RunDescription& run, const ErosionSimulation& sim,
                             const ExportResult& ex)
        {
            const SimulationStatistics& st = sim.statistics();
            const SimulationConfig& c = run.config;
            const TerrainScale& sc = run.scale;
            const double area = sc.cellSizeMeters * sc.cellSizeMeters;
            const double domainArea = area * static_cast<double>(run.raster.width) * static_cast<double>(run.raster.height);
            const auto mm = [&](double volume) { return fixed(volume / domainArea * 1000.0, 4) + " mm"; };

            std::ostringstream r;
            r << "==============================================================\n"
              << " SHALLOW-WATER AND TERRAIN EROSION - SIMULATION REPORT\n"
              << "==============================================================\n"
              << "Generated:            " << localTimestamp() << "\n"
              << "Program:              " << run.programVersion << "\n"
              << "Algorithm:            Virtual Pipes Method (Mei et al. 2007) + Manning friction (Bates et al. 2010)\n"
              << "                      + capacity-based erosion/deposition + conservative sediment transport\n"
              << "Execution:            sequential CPU (no OpenMP, no SIMD intrinsics, no GPU)\n\n";

            r << "[INPUT]\n"
              << "File:                 " << run.inputPath << "\n"
              << "Format:               " << toString(run.raster.format) << ", " << run.raster.bitsPerChannel
              << " bit/channel, " << run.raster.channels << " channel(s)\n"
              << "Resolution:           " << run.raster.width << " x " << run.raster.height << " px\n"
              << "Grayscale:            " << run.raster.grayscaleMethod << "\n"
              << "Interpretation:       " << (run.inverted ? "INVERTED (white = lowest)" : "black = lowest, white = highest")
              << "\n";
            if (run.raster.format == ImageFormat::Jpeg)
                r << "Note:                 JPEG is lossy (8x8 block artefacts become small false terrain steps)\n";
            r << "\n[TERRAIN SCALE] (model assumption, not measured geography)\n"
              << "Cell size:            " << fmt(sc.cellSizeMeters) << " m\n"
              << "Elevation range:      " << fmt(sc.minElevationMeters) << " .. " << fmt(sc.maxElevationMeters) << " m\n"
              << "Domain size:          " << fmt(sc.cellSizeMeters * run.raster.width / 1000.0) << " x "
              << fmt(sc.cellSizeMeters * run.raster.height / 1000.0) << " km (" << fmt(domainArea / 1.0e6) << " km2)\n"
              << "Input height step:    " << sci(sc.elevationRange() / (run.raster.bitsPerChannel == 16 ? 65535.0 : 255.0))
              << " m per gray level\n\n";

            r << "[RAINFALL]\n"
              << "Total rainfall:       " << fmt(c.rainfall.totalMillimeters) << " L/m2 = " << fmt(c.rainfall.totalMillimeters)
              << " mm = " << fmt(c.rainfall.totalMeters()) << " m of water\n"
              << "Rain duration:        " << duration(c.rainSeconds()) << "\n"
              << "Intensity:            " << fmt(c.rainfall.intensityMillimetersPerHour()) << " mm/h = "
              << sci(c.rainfall.intensityMetersPerSecond()) << " m/s (uniform in space and time)\n"
              << "Runoff after rain:    " << duration(c.runoffSeconds()) << " (no rainfall)\n"
              << "Total simulated time: " << duration(c.totalSeconds()) << "\n\n";

            r << "[MODEL PARAMETERS]\n"
              << "Gravity g:            " << c.flow.gravity << " m/s2\n"
              << "Manning n:            " << c.flow.manningN << " s/m^(1/3)\n"
              << "Courant number:       " << c.flow.courantNumber << " (dt = C*l / max(2*sqrt(g d) + |u|))\n"
              << "Max time step:        " << c.flow.maxTimeStepSeconds << " s\n"
              << "Boundary:             " << toString(c.flow.boundary) << "\n"
              << "Infiltration:         " << c.flow.infiltrationMillimetersPerHour << " mm/h\n"
              << "Evaporation:          " << c.flow.evaporationMillimetersPerHour << " mm/h\n"
              << "Erosion enabled:      " << (c.erosion.enabled ? "yes" : "no") << "\n"
              << "Capacity Kc:          " << c.erosion.capacityCoefficient << " s/m (min sin(alpha) "
              << c.erosion.minimumSlope << ", max concentration " << c.erosion.maxConcentration << ")\n"
              << "Dissolving Ks:        " << c.erosion.dissolvingRate << " 1/s\n"
              << "Deposition Kd:        " << c.erosion.depositionRate << " 1/s\n"
              << "Erodible layer:       "
              << (c.erosion.erodibleDepthMeters > 0.0 ? fmt(c.erosion.erodibleDepthMeters) + " m" : std::string("unlimited")) << "\n"
              << "Settle at end:        " << (c.erosion.settleSuspendedSedimentAtEnd ? "yes" : "no") << "\n\n";

            r << "[NUMERICS]\n"
              << "Time steps:           " << st.steps << "\n"
              << "dt min/mean/max:      " << fmt(st.minTimeStep) << " / " << fmt(st.meanTimeStep) << " / "
              << fmt(st.maxTimeStep) << " s\n"
              << "Simulated time:       " << duration(st.simulatedSeconds) << "\n"
              << "Execution time:       " << fixed(st.wallSeconds, 2) << " s\n";
            if (st.aborted)
                r << "ABORTED:              " << st.abortReason << "\n";
            r << "\n";

            r << "[WATER BALANCE] (m3; mm = volume / domain area)\n"
              << "Rain added:           " << fixed(st.rainVolume, 3) << " m3 (" << mm(st.rainVolume) << ")\n"
              << "Boundary outflow:     " << fixed(st.boundaryWaterOutflow, 3) << " m3 (" << mm(st.boundaryWaterOutflow) << ")\n"
              << "Infiltration:         " << fixed(st.infiltratedVolume, 3) << " m3\n"
              << "Evaporation:          " << fixed(st.evaporatedVolume, 3) << " m3\n"
              << "Water on terrain:     " << fixed(st.finalWaterVolume, 3) << " m3 (" << mm(st.finalWaterVolume) << ")\n"
              << "Balance error:        " << sci(st.waterBalanceError()) << " m3 (relative " << sci(st.waterBalanceRelativeError())
              << ")\n"
              << "Peak water depth:     " << fmt(st.peakWaterDepth) << " m\n"
              << "Peak water speed:     " << fmt(st.peakSpeed) << " m/s\n\n";

            r << "[MATERIAL BALANCE] (terrain + suspended sediment, m3 of terrain material)\n"
              << "Gross eroded:         " << fixed(st.grossErodedVolume, 3) << " m3 (sum over all steps)\n"
              << "Gross deposited:      " << fixed(st.grossDepositedVolume, 3) << " m3 (sum over all steps)\n"
              << "Suspended at end:     " << fixed(st.suspendedAtEndVolume, 3) << " m3 ("
              << (c.erosion.settleSuspendedSedimentAtEnd ? "settled in place" : "left in water, NOT in output map") << ")\n"
              << "Sediment outflow:     " << fixed(st.sedimentBoundaryOutflow, 3) << " m3\n"
              << "Balance error:        " << sci(st.materialBalanceError) << " m3 (relative to gross erosion "
              << sci(st.materialBalanceRelativeError()) << ")\n\n";

            r << "[TERRAIN CHANGE] dh = final - initial\n"
              << "Net lowered volume:   " << fixed(st.netLoweredVolume, 3) << " m3 (" << mm(st.netLoweredVolume) << ")\n"
              << "Net raised volume:    " << fixed(st.netRaisedVolume, 3) << " m3 (" << mm(st.netRaisedVolume) << ")\n"
              << "Max lowering:         " << fmt(st.maxLowering) << " m\n"
              << "Max raise:            " << fmt(st.maxRaise) << " m\n"
              << "Mean |dh|:            " << sci(st.meanAbsChange) << " m\n"
              << "Cells |dh| > 1 mm:    " << st.cellsChangedMoreThan1mm << " of "
              << static_cast<std::int64_t>(run.raster.width) * run.raster.height << "\n"
              << "Difference colors:    +-" << sci(ex.differenceColorScale)
              << " m = full color (99th percentile of |dh|; larger values saturated)\n\n";

            r << "[VALIDITY]\n"
              << "All values finite:    " << (st.allFinite ? "yes" : "NO") << "\n"
              << "Min water depth:      " << sci(st.minWaterDepth) << " m\n"
              << "Min sediment:         " << sci(st.minSediment) << " m\n"
              << "Output range check:   " << (ex.erodedRange.clipped() ? "VALUES OUTSIDE [min, max] WERE CLIPPED IN PNG" : "all values inside [min, max]")
              << " (lowest " << fmt(ex.erodedRange.lowestMeters, 6) << " m, highest " << fmt(ex.erodedRange.highestMeters, 6)
              << " m)\n\n";

            r << "[OUTPUT FILES]\n";
            for (const auto& f : ex.files)
                r << "  " << f << "\n";
            r << "  " << path << "\n";
            if (!ex.reportJson.empty())
                r << "  " << ex.reportJson << "\n";
            r << "\n[WARNINGS]\n";
            if (ex.warnings.empty())
                r << "  none\n";
            for (const auto& w : ex.warnings)
                r << "  - " << w << "\n";

            std::ofstream out(pathFromUtf8(path), std::ios::trunc);
            if (!out)
                throw std::runtime_error("Ne mogu zapisati izvjestaj: " + path);
            out << r.str();
        }

        void writeJsonReport(const std::string& path, const RunDescription& run, const ErosionSimulation& sim,
                             const ExportResult& ex)
        {
            const SimulationStatistics& st = sim.statistics();
            const SimulationConfig& c = run.config;
            JsonObject input, scale, rain, flow, erosion, numerics, water, material, terrain, validity, root;
            input.str("file", run.inputPath)
                .str("format", toString(run.raster.format))
                .integer("width", run.raster.width)
                .integer("height", run.raster.height)
                .integer("channels", run.raster.channels)
                .integer("bitsPerChannel", run.raster.bitsPerChannel)
                .str("grayscale", run.raster.grayscaleMethod)
                .boolean("inverted", run.inverted);
            scale.num("cellSizeMeters", run.scale.cellSizeMeters)
                .num("minElevationMeters", run.scale.minElevationMeters)
                .num("maxElevationMeters", run.scale.maxElevationMeters);
            rain.num("totalLitersPerSquareMeter", c.rainfall.totalMillimeters)
                .num("durationHours", c.rainfall.durationHours)
                .num("intensityMillimetersPerHour", c.rainfall.intensityMillimetersPerHour())
                .num("intensityMetersPerSecond", c.rainfall.intensityMetersPerSecond())
                .num("runoffHours", c.runoffHours)
                .num("totalSimulatedSeconds", c.totalSeconds());
            flow.num("gravity", c.flow.gravity)
                .num("manningN", c.flow.manningN)
                .num("courantNumber", c.flow.courantNumber)
                .num("maxTimeStepSeconds", c.flow.maxTimeStepSeconds)
                .str("boundary", toString(c.flow.boundary))
                .num("infiltrationMillimetersPerHour", c.flow.infiltrationMillimetersPerHour)
                .num("evaporationMillimetersPerHour", c.flow.evaporationMillimetersPerHour);
            erosion.boolean("enabled", c.erosion.enabled)
                .num("capacityCoefficient", c.erosion.capacityCoefficient)
                .num("minimumSlope", c.erosion.minimumSlope)
                .num("maxConcentration", c.erosion.maxConcentration)
                .num("dissolvingRate", c.erosion.dissolvingRate)
                .num("depositionRate", c.erosion.depositionRate)
                .num("erodibleDepthMeters", c.erosion.erodibleDepthMeters)
                .boolean("settleSuspendedSedimentAtEnd", c.erosion.settleSuspendedSedimentAtEnd);
            numerics.integer("steps", st.steps)
                .num("minTimeStep", st.minTimeStep)
                .num("meanTimeStep", st.meanTimeStep)
                .num("maxTimeStep", st.maxTimeStep)
                .num("simulatedSeconds", st.simulatedSeconds)
                .num("wallSeconds", st.wallSeconds)
                .boolean("aborted", st.aborted)
                .str("abortReason", st.abortReason);
            water.num("rainVolume", st.rainVolume)
                .num("boundaryOutflow", st.boundaryWaterOutflow)
                .num("infiltrated", st.infiltratedVolume)
                .num("evaporated", st.evaporatedVolume)
                .num("finalWater", st.finalWaterVolume)
                .num("balanceError", st.waterBalanceError())
                .num("balanceRelativeError", st.waterBalanceRelativeError())
                .num("peakDepth", st.peakWaterDepth)
                .num("peakSpeed", st.peakSpeed);
            material.num("grossEroded", st.grossErodedVolume)
                .num("grossDeposited", st.grossDepositedVolume)
                .num("suspendedAtEnd", st.suspendedAtEndVolume)
                .num("settledAtEnd", st.settledAtEndVolume)
                .num("sedimentOutflow", st.sedimentBoundaryOutflow)
                .num("balanceError", st.materialBalanceError)
                .num("balanceRelativeError", st.materialBalanceRelativeError());
            terrain.num("netLoweredVolume", st.netLoweredVolume)
                .num("netRaisedVolume", st.netRaisedVolume)
                .num("maxLowering", st.maxLowering)
                .num("maxRaise", st.maxRaise)
                .num("meanAbsChange", st.meanAbsChange)
                .integer("cellsChangedMoreThan1mm", st.cellsChangedMoreThan1mm)
                .num("differenceColorScale", ex.differenceColorScale);
            validity.boolean("allFinite", st.allFinite)
                .num("minWaterDepth", st.minWaterDepth)
                .num("minSediment", st.minSediment)
                .integer("pngBelowMinimum", ex.erodedRange.belowMinimum)
                .integer("pngAboveMaximum", ex.erodedRange.aboveMaximum)
                .num("lowestMeters", ex.erodedRange.lowestMeters)
                .num("highestMeters", ex.erodedRange.highestMeters);
            root.str("generated", localTimestamp())
                .str("program", run.programVersion)
                .str("units", "lengths m, volumes m3, time s, rain mm (= L/m2)")
                .object("input", input)
                .object("terrainScale", scale)
                .object("rainfall", rain)
                .object("waterFlow", flow)
                .object("erosion", erosion)
                .object("numerics", numerics)
                .object("waterBalance", water)
                .object("materialBalance", material)
                .object("terrainChange", terrain)
                .object("validity", validity)
                .strings("files", ex.files)
                .strings("warnings", ex.warnings);
            std::ofstream out(pathFromUtf8(path), std::ios::trunc);
            if (!out)
                throw std::runtime_error("Ne mogu zapisati izvjestaj: " + path);
            out << root.text() << "\n";
        }

        void writeCsv(const std::string& path, const Grid2D<double>& g)
        {
            std::ofstream out(pathFromUtf8(path), std::ios::trunc);
            if (!out)
                throw std::runtime_error("Ne mogu zapisati " + path);
            out << std::setprecision(17); // 17 značajnih cifara = double bez gubitka
            for (int y = 0; y < g.height(); ++y)
            {
                for (int x = 0; x < g.width(); ++x)
                    out << (x ? "," : "") << g(x, y);
                out << '\n';
            }
        }
    }

    ExportResult exportResults(const ExportOptions& options, const RunDescription& run, const ErosionSimulation& sim)
    {
        ExportResult ex;
        const fs::path dir = pathFromUtf8(options.outputDirectory);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec || !fs::is_directory(dir))
            throw std::runtime_error("Ne mogu napraviti izlazni folder: " + options.outputDirectory);

        const std::string base = options.baseName.empty() ? std::string("terrain") : options.baseName;
        const auto file = [&](const std::string& suffix) { return joinPath(options.outputDirectory, base + suffix); };

        // Zaštita: izlaz nikad ne smije prepisati ulaznu datoteku.
        const fs::path inputPath = pathFromUtf8(run.inputPath);
        for (const char* suffix : {"_original.png", "_eroded.png", "_difference.png", "_comparison.png", "_water_max_depth.png"})
        {
            if (fs::exists(inputPath, ec) && fs::exists(pathFromUtf8(file(suffix)), ec) &&
                fs::equivalent(inputPath, pathFromUtf8(file(suffix)), ec))
                throw std::runtime_error("Izlaz bi prepisao ulaznu datoteku: " + run.inputPath);
        }

        const SimulationState& state = sim.state();
        const Grid2D<double>& initial = sim.initialTerrain();
        const Grid2D<double>& finalTerrain = state.terrain;
        const int W = finalTerrain.width(), H = finalTerrain.height();

        Grid2D<double> difference(W, H);
        std::vector<double> changed;
        for (std::size_t i = 0; i < finalTerrain.size(); ++i)
        {
            difference[i] = finalTerrain[i] - initial[i];
            if (difference[i] != 0.0)
                changed.push_back(std::abs(difference[i]));
        }

        // 1) height mape: 16-bitni PNG, ista skala (i ista konvencija crno/bijelo) kao ulaz
        const std::string originalPng = file("_original.png");
        writeHeightmapPng16(originalPng, initial, run.scale, run.inverted);
        ex.files.push_back(originalPng);

        ex.erodedPng = file("_eroded.png");
        ex.erodedRange = writeHeightmapPng16(ex.erodedPng, finalTerrain, run.scale, run.inverted);
        ex.files.push_back(ex.erodedPng);
        if (ex.erodedRange.clipped())
        {
            std::ostringstream w;
            w << "Erodirani teren izlazi iz visinskog raspona [" << run.scale.minElevationMeters << ", "
              << run.scale.maxElevationMeters << "] m: " << ex.erodedRange.belowMinimum << " celija ispod (najnize "
              << ex.erodedRange.lowestMeters << " m), " << ex.erodedRange.aboveMaximum << " iznad (najvise "
              << ex.erodedRange.highestMeters << " m). U PNG-u su odsjecene; tacne vrijednosti su u "
              << base << "_eroded_m.npy";
            ex.warnings.push_back(w.str());
        }

        // 2) vizualizacije (ne mijenjaju height mapu)
        ex.differenceColorScale = percentile(changed, 0.99);
        const std::string diffPng = file("_difference.png");
        writePngRgb8(diffPng, W, H, renderDifference(difference, ex.differenceColorScale));
        ex.files.push_back(diffPng);

        // z-faktor (vertikalno preuveličanje samo za prikaz) iz 95. percentila nagiba originalnog terena
        {
            std::vector<double> slopes;
            slopes.reserve(initial.size());
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    double gx, gy;
                    gradientAt(initial, x, y, run.scale.cellSizeMeters, gx, gy);
                    slopes.push_back(std::sqrt(gx * gx + gy * gy));
                }
            const double p95 = percentile(slopes, 0.95);
            ex.hillshadeZFactor = p95 > 0.0 ? std::clamp(1.0 / p95, 1.0, 50.0) : 1.0;
        }
        const std::vector<std::uint8_t> shadeA = renderHillshade(initial, run.scale.cellSizeMeters, ex.hillshadeZFactor);
        const std::vector<std::uint8_t> shadeB = renderHillshade(finalTerrain, run.scale.cellSizeMeters, ex.hillshadeZFactor);
        const int gap = 8;
        const int CW = 2 * W + gap;
        std::vector<std::uint8_t> comparison(static_cast<std::size_t>(CW) * static_cast<std::size_t>(H) * 3, 255);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 3; ++c)
                {
                    const std::size_t src = (static_cast<std::size_t>(y) * W + x) * 3 + c;
                    comparison[(static_cast<std::size_t>(y) * CW + x) * 3 + c] = shadeA[src];
                    comparison[(static_cast<std::size_t>(y) * CW + x + W + gap) * 3 + c] = shadeB[src];
                }
        const std::string comparisonPng = file("_comparison.png");
        writePngRgb8(comparisonPng, CW, H, comparison);
        ex.files.push_back(comparisonPng);

        const std::string waterPng = file("_water_max_depth.png");
        writePngRgb8(waterPng, W, H, renderWaterDepth(state.maxWaterDepth));
        ex.files.push_back(waterPng);

        // 3) sirovi podaci bez gubitka preciznosti (float64)
        const std::string npyOriginal = file("_original_m.npy");
        const std::string npyEroded = file("_eroded_m.npy");
        const std::string npyDiff = file("_difference_m.npy");
        const std::string npyWater = file("_water_max_depth_m.npy");
        saveNpy(npyOriginal, initial);
        saveNpy(npyEroded, finalTerrain);
        saveNpy(npyDiff, difference);
        saveNpy(npyWater, state.maxWaterDepth);
        for (const auto& f : {npyOriginal, npyEroded, npyDiff, npyWater})
            ex.files.push_back(f);
        if (options.writeCsv)
        {
            const std::string csv = file("_eroded_m.csv");
            writeCsv(csv, finalTerrain);
            ex.files.push_back(csv);
        }

        if (run.raster.format == ImageFormat::Jpeg)
            ex.warnings.push_back("Ulaz je JPEG (kompresija s gubicima): artefakti kompresije su tretirani kao stvarne neravnine terena.");
        if (sim.statistics().aborted)
            ex.warnings.push_back("Simulacija je prekinuta prije kraja: " + sim.statistics().abortReason);
        if (!sim.statistics().allFinite)
            ex.warnings.push_back("Rezultat sadrzi NaN/Inf vrijednosti!");

        // 4) izvještaji
        ex.reportTxt = joinPath(options.outputDirectory, "simulation_report.txt");
        ex.reportJson = joinPath(options.outputDirectory, "simulation_report.json");
        writeJsonReport(ex.reportJson, run, sim, ex);
        writeTextReport(ex.reportTxt, run, sim, ex);
        return ex;
    }
}
