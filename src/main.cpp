// prs_erosion – konzolni simulator hidrauličke erozije terena metodom virtuelnih cijevi (serijski, CPU).
//
// Ulaz:  height mapa (PNG ili JPEG) + ukupna kiša [L/m²] + trajanje kiše [h] (+ opcioni period oticanja)
// Izlaz: erodirana height mapa (16-bitni PNG, iste dimenzije i ista visinska skala) + izvještaj i vizualizacije
//
// Načini rada:
//   prs_erosion                         interaktivni meni ([1] Učitaj heightmapu, [2] Izlaz; sva pitanja na bosanskom)
//   prs_erosion --input mapa.png --rain 100 --hours 5 [opcije]      bez pitanja (skripte, testovi)
//   prs_erosion --help                  sve opcije
//
// Ovaj fajl sadrži SAMO konzolni interfejs: numerika je u ErosionSimulation (ne zna za konzolu ni slike),
// učitavanje u HeightmapIO, izvoz u ResultExport.

#include "ConsoleInput.hpp"
#include "ErosionSimulation.hpp"
#include "HeightmapIO.hpp"
#include "ImageWriter.hpp"
#include "ResultExport.hpp"
#include "LocalTerrainServer.hpp"
#include "TerrainMetadata.hpp"

#include <chrono>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/select.h> // provjera da li je korisnik pritisnuo Enter dok program čeka preglednik
#endif

namespace
{
    using namespace prs;
    using prs::console::Prompter;

    constexpr const char* kProgramVersion = "prs_erosion 2.0 (faza 2: serijska simulacija erozije, virtuelne cijevi)";

    // ------------------------------------------------------------------ formatiranje
    std::string num(double v, int precision = 6)
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
        os << std::scientific << std::setprecision(2) << v;
        return os.str();
    }

    std::string formatDuration(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0.0)
            return "?";
        const long s = static_cast<long>(std::lround(seconds));
        std::ostringstream os;
        if (s >= 3600)
            os << s / 3600 << " h " << (s % 3600) / 60 << " min";
        else if (s >= 60)
            os << s / 60 << " min " << s % 60 << " s";
        else
            os << fixed(seconds, 1) << " s";
        return os.str();
    }

    void section(std::ostream& out, const std::string& title)
    {
        out << "\n----------------------------------------\n" << title << "\n----------------------------------------\n";
    }

    void printBanner(std::ostream& out)
    {
        out << "================================================\n"
            << "      SHALLOW-WATER AND TERRAIN EROSION\n"
            << "                  PRS PROJECT\n"
            << "================================================\n";
    }

    // ------------------------------------------------------------------ jedan "posao" simulacije
    struct Job
    {
        std::string inputPath;
        TerrainScale scale;
        bool invert = false;
        SimulationConfig config;
        std::string outputDirectory; // prazno = output/<ime_ulaza>
        bool writeCsv = false;
        std::string terrainJson;     // --terrain-json: metapodaci iz birača terena (prazno = traži <ime>.terrain.json)
    };

    std::string baseNameOf(const std::string& path)
    {
        return pathToUtf8(pathFromUtf8(path).stem());
    }

    std::string defaultOutputDirectory(const std::string& inputPath)
    {
        return pathToUtf8(std::filesystem::path("output") / pathFromUtf8(baseNameOf(inputPath)));
    }

    // ------------------------------------------------------------------ birač terena (tools/terrain_picker.html)
    namespace fs = std::filesystem;

    fs::path downloadsFolder()
    {
#if defined(_WIN32)
        wchar_t buffer[2048];
        const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buffer, static_cast<DWORD>(std::size(buffer)));
        if (n > 0 && n < std::size(buffer))
            return fs::path(buffer) / L"Downloads";
        return {};
#else
        const char* home = std::getenv("HOME");
        return home ? fs::path(home) / "Downloads" : fs::path{};
#endif
    }

    fs::path executableDirectory()
    {
#if defined(_WIN32)
        wchar_t buffer[32768];
        const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (n > 0 && n < std::size(buffer))
            return fs::path(buffer).parent_path();
        return {};
#elif defined(__linux__)
        std::error_code ec;
        const fs::path p = fs::read_symlink("/proc/self/exe", ec);
        return ec ? fs::path{} : p.parent_path();
#else
        return {};
#endif
    }

    // Traži tools/terrain_picker.html u folderu programa, tekućem folderu i njihovim roditeljima
    // (program se obično nalazi u build-vs\Release, dva nivoa ispod korijena projekta).
    std::optional<fs::path> findPickerHtml()
    {
        for (const fs::path& start : {executableDirectory(), fs::current_path()})
        {
            fs::path dir = start;
            for (int level = 0; level < 7 && !dir.empty(); ++level)
            {
                std::error_code ec;
                const fs::path candidate = dir / "tools" / "terrain_picker.html";
                if (fs::is_regular_file(candidate, ec))
                    return candidate;
                const fs::path parent = dir.parent_path();
                if (parent == dir)
                    break;
                dir = parent;
            }
        }
        return std::nullopt;
    }

    bool openInBrowser(const fs::path& file)
    {
#if defined(_WIN32)
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
#else
        std::string quoted = "'";
        for (const char c : pathToUtf8(file))
            quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        quoted += "'";
#if defined(__APPLE__)
        const std::string command = "open " + quoted + " >/dev/null 2>&1 &";
#else
        const std::string command = "xdg-open " + quoted + " >/dev/null 2>&1 &";
#endif
        return std::system(command.c_str()) == 0;
#endif
    }

    bool openUrl(const std::string& url)
    {
#if defined(_WIN32)
        const std::wstring wide(url.begin(), url.end()); // URL je ASCII (http://127.0.0.1:port/?token=hex)
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__APPLE__)
        return std::system(("open '" + url + "' >/dev/null 2>&1 &").c_str()) == 0;
#else
        return std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()) == 0;
#endif
    }

    // Da li je korisnik nešto otkucao u konzoli (bez blokiranja) – za "Enter = odustani" dok program čeka preglednik.
    bool consoleInputAvailable()
    {
#if defined(_WIN32)
        const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(in, &mode))
        {
            // ulaz nije konzola (preusmjeren iz fajla/cijevi): ima li nepročitanih bajtova?
            DWORD available = 0;
            return !PeekNamedPipe(in, nullptr, 0, nullptr, &available, nullptr) || available > 0;
        }
        DWORD events = 0;
        if (!GetNumberOfConsoleInputEvents(in, &events) || events == 0)
            return false;
        std::vector<INPUT_RECORD> records(events);
        DWORD read = 0;
        PeekConsoleInputW(in, records.data(), events, &read);
        for (DWORD i = 0; i < read; ++i)
            if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown &&
                records[i].Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
                return true;
        return false;
#else
        fd_set set;
        FD_ZERO(&set);
        FD_SET(0, &set);
        timeval tv{};
        return ::select(1, &set, nullptr, nullptr, &tv) > 0;
#endif
    }

    // Sprema primljeni teren u tereni/<ime>.png i tereni/<ime>.terrain.json (bez prepisivanja postojećih).
    std::string saveReceivedTerrain(const ReceivedTerrain& r)
    {
        const fs::path folder = "tereni";
        fs::create_directories(folder);
        std::string name = r.name;
        for (int k = 2; fs::exists(folder / (name + ".png")) || fs::exists(folder / (name + ".terrain.json")); ++k)
            name = r.name + "_" + std::to_string(k);
        const fs::path png = folder / (name + ".png");
        {
            std::ofstream f(png, std::ios::binary);
            f.write(reinterpret_cast<const char*>(r.png.data()), static_cast<std::streamsize>(r.png.size()));
            if (!f)
                throw std::runtime_error("Ne mogu zapisati " + pathToUtf8(png));
        }
        {
            std::ofstream f(terrainMetadataSidecar(png), std::ios::binary);
            f << r.json;
            if (!f)
                throw std::runtime_error("Ne mogu zapisati metapodatke terena");
        }
        return pathToUtf8(png);
    }

    // Pokreće lokalni server, otvara birač u pregledniku i čeka da stranica pošalje teren.
    // Vraća putanju spremljenog PNG-a; nullopt ako server nije pokrenut ili je korisnik pritisnuo Enter (odustao).
    std::optional<std::string> receiveFromBrowser(Prompter& ask, std::ostream& out, const fs::path& html, bool& eof,
                                                  bool& pageOpened)
    {
        TerrainReceiver server;
        if (const std::string error = server.start(html); !error.empty())
        {
            out << "\n  ! Lokalni server nije pokrenut (" << error << "); teren ćete preuzeti ručno.\n";
            return std::nullopt;
        }
        pageOpened = openUrl(server.url());
        out << "\n" << (pageOpened ? "Otvaram birač terena u pregledniku:" : "  ! Preglednik se nije otvorio automatski. Otvorite ovu adresu:")
            << "\n  " << server.url() << "\n\n"
            << "Odaberite područje i kliknite „Generiši teren“ – podaci stižu u program automatski.\n"
            << "Čekam teren iz preglednika... (Enter = odustani i učitaj fajl ručno)\n"
            << std::flush;
        while (true)
        {
            server.poll(200);
            if (server.complete())
                break;
            if (consoleInputAvailable())
            {
                if (!ask.readLine(""))
                    eof = true;
                else
                    out << "Čekanje je prekinuto.\n";
                return std::nullopt;
            }
        }
        const ReceivedTerrain received = server.take();
        // Odgovori stranici su već poslani; server se gasi kad `server` izađe iz opsega.
        try
        {
            const std::string png = saveReceivedTerrain(received);
            out << "\nTeren je primljen iz preglednika (" << received.png.size() / 1024 << " KB) i spremljen:\n  "
                << pathToUtf8(fs::absolute(pathFromUtf8(png))) << "\n  " << pathToUtf8(fs::absolute(terrainMetadataSidecar(pathFromUtf8(png))))
                << "\n";
            return png;
        }
        catch (const std::exception& e)
        {
            out << "\n  ! " << e.what() << "\n";
            return std::nullopt;
        }
    }

    // Ako uz sliku postoje metapodaci iz birača terena (<ime>.terrain.json, ili eksplicitni --terrain-json),
    // ispiše ih i vrati skalu (uz preračun veličine ćelije ako je slika smanjena). `explicitJson` prazan = traži pored slike.
    // Vraća nullopt ako ih nema ili su neupotrebljivi (uz razlog u ispisu). Greške eksplicitnog fajla se prijavljuju.
    std::optional<TerrainMetadata> findTerrainMetadata(std::ostream& out, const std::string& imagePath, const RasterInfo& info,
                                                       const std::string& explicitJson, std::string* explicitError = nullptr)
    {
        const bool isExplicit = !explicitJson.empty();
        const fs::path jsonPath = isExplicit ? pathFromUtf8(explicitJson) : terrainMetadataSidecar(pathFromUtf8(imagePath));
        std::error_code ec;
        if (!fs::is_regular_file(jsonPath, ec))
        {
            if (isExplicit && explicitError)
                *explicitError = "Metapodaci terena ne postoje: " + explicitJson;
            return std::nullopt;
        }
        try
        {
            TerrainMetadata meta = loadTerrainMetadata(jsonPath);
            const double originalCell = meta.scale.cellSizeMeters;
            const int originalW = meta.width, originalH = meta.height;
            bool rescaled = false;
            const std::string reason = adaptMetadataToImage(meta, info.width, info.height, &rescaled);
            if (!reason.empty())
            {
                out << "\n  ! Metapodaci terena (" << pathToUtf8(jsonPath.filename()) << ") se ne mogu primijeniti: " << reason << ".\n";
                if (isExplicit && explicitError)
                    *explicitError = "Metapodaci terena se ne mogu primijeniti: " + reason;
                return std::nullopt;
            }
            out << "\nPronađeni metapodaci terena (" << pathToUtf8(jsonPath.filename()) << "):\n"
                << "  Veličina ćelije: " << num(meta.scale.cellSizeMeters, 6) << " m\n"
                << "  Visina terena: " << num(meta.scale.minElevationMeters, 7) << " .. " << num(meta.scale.maxElevationMeters, 7) << " m\n";
            if (meta.hasBounds)
                out << "  Područje: " << fixed(meta.south, 4) << " .. " << fixed(meta.north, 4) << " N, " << fixed(meta.west, 4) << " .. "
                    << fixed(meta.east, 4) << " E\n";
            if (!meta.source.empty())
                out << "  Izvor: " << meta.source << "\n";
            if (rescaled)
                out << "  Napomena: slika je promijenjena sa " << originalW << " x " << originalH << " na " << info.width << " x "
                    << info.height << ", pa je veličina ćelije preračunata sa " << num(originalCell, 6) << " m na "
                    << num(meta.scale.cellSizeMeters, 6) << " m.\n";
            return meta;
        }
        catch (const std::exception& e)
        {
            out << "\n  ! Metapodaci terena (" << pathToUtf8(jsonPath.filename()) << ") nisu upotrebljivi: " << e.what() << "\n";
            if (isExplicit && explicitError)
                *explicitError = e.what();
            return std::nullopt;
        }
    }

    void printRasterInfo(std::ostream& out, const RasterInfo& info)
    {
        out << "\nHeightmapa je uspješno učitana!\n\n"
            << "Rezolucija: " << info.width << " x " << info.height << (info.width != info.height ? " (nije kvadratna)" : "") << "\n"
            << "Format: " << toString(info.format) << "\n"
            << "Broj kanala: " << info.channels << "\n"
            << "Dubina boje: " << info.bitsPerChannel << " bita po kanalu\n"
            << "Sivi tonovi: " << info.grayscaleMethod << "\n";
        if (info.format == ImageFormat::Jpeg)
            out << "Napomena: JPEG je kompresija s gubicima - artefakti kompresije postaju sitne lažne stepenice u terenu.\n";
    }

    // Rubni uslov na bosanskom za ispis (interni nazivi closed/open ostaju u izvještajima i opcijama).
    const char* boundaryText(BoundaryCondition b)
    {
        return b == BoundaryCondition::Open ? "otvoren (voda može napustiti mapu)" : "zatvoren (zidovi)";
    }

    void printSummary(std::ostream& out, const Job& job, const RasterInfo& info)
    {
        const SimulationConfig& c = job.config;
        section(out, "SAŽETAK SIMULACIJE");
        out << "Kiša: " << num(c.rainfall.totalMillimeters) << " L/m2 (= " << num(c.rainfall.totalMillimeters) << " mm)\n"
            << "Trajanje kiše: " << num(c.rainfall.durationHours) << " h\n"
            << "Intenzitet kiše: " << num(c.rainfall.intensityMillimetersPerHour()) << " mm/h\n"
            << "Oticanje nakon kiše: " << num(c.runoffHours) << " h (bez padavina)\n"
            << "Simulirano vrijeme: " << num(c.totalSeconds()) << " s\n"
            << "Veličina mreže: " << info.width << " x " << info.height << " (ćelije od " << num(job.scale.cellSizeMeters)
            << " m, " << num(job.scale.cellSizeMeters * info.width / 1000.0, 4) << " x "
            << num(job.scale.cellSizeMeters * info.height / 1000.0, 4) << " km)\n"
            << "Visina terena: " << num(job.scale.minElevationMeters) << " .. " << num(job.scale.maxElevationMeters) << " m"
            << (job.invert ? " (invertovano: bijelo = najniže)" : "") << "\n"
            << "Rubni uslov: " << boundaryText(c.flow.boundary) << "\n"
            << "Algoritam: metoda virtuelnih cijevi (Mei 2007) + Manningovo trenje, hidraulička erozija\n"
            << "Parametri: n = " << c.flow.manningN << ", Kc = " << c.erosion.capacityCoefficient
            << " s/m, Ks = " << c.erosion.dissolvingRate << " 1/s, Kd = " << c.erosion.depositionRate
            << " 1/s, erodibilni sloj = "
            << (c.erosion.erodibleDepthMeters > 0.0 ? num(c.erosion.erodibleDepthMeters) + " m" : std::string("neograničen")) << "\n"
            << "Izvršavanje: serijski, CPU\n"
            << "Izlazni folder: " << (job.outputDirectory.empty() ? defaultOutputDirectory(job.inputPath) : job.outputDirectory) << "\n";
    }

    ProgressCallback consoleProgress(std::ostream& out)
    {
        return [&out](const SimulationProgress& p) {
            const double fraction = p.fraction();
            const double eta = fraction > 0.0 ? p.wallSeconds * (1.0 - fraction) / fraction : -1.0;
            std::ostringstream line;
            line << "Napredak: " << std::setw(5) << fixed(100.0 * fraction, 1) << "%"
                 << " | t = " << fixed(p.simulatedSeconds / 3600.0, 2) << " / " << fixed(p.totalSeconds / 3600.0, 2) << " h"
                 << (p.simulatedSeconds >= p.totalSeconds ? " (gotovo) " : (p.raining ? " (kiša)   " : " (oticanje)"))
                 << " | koraka " << p.step << " | dt " << fixed(p.lastTimeStep, 3)
                 << " s | proteklo " << formatDuration(p.wallSeconds) << " | preostalo ~" << formatDuration(eta);
            // Povremeni "otkucaj" prepisuje isti red (\r); prag od 5 % ostaje kao trajni red.
            out << "\r" << line.str() << (p.milestone ? "\n" : "          ") << std::flush;
        };
    }

    void printResults(std::ostream& out, const Job& job, const ErosionSimulation& sim, const ExportResult& ex)
    {
        const SimulationStatistics& st = sim.statistics();
        section(out, "REZULTATI");
        out << "Ulaz: " << job.inputPath << "\n"
            << "Izlaz: " << ex.erodedPng << "\n\n"
            << "Simulirano vrijeme: " << num(st.simulatedSeconds / 3600.0) << " h (" << num(job.config.rainfall.durationHours)
            << " h kiše + " << num(job.config.runoffHours) << " h oticanja)\n"
            << "Vrijeme izvršavanja: " << fixed(st.wallSeconds, 2) << " s (" << st.steps << " vremenskih koraka, dt "
            << num(st.minTimeStep, 3) << " .. " << num(st.maxTimeStep, 3) << " s)\n\n"
            << "Ukupno erodirani materijal: " << fixed(st.grossErodedVolume, 2) << " m3 (zbir svih događaja erozije)\n"
            << "Ukupno istaloženi materijal: " << fixed(st.grossDepositedVolume + st.settledAtEndVolume, 2)
            << " m3 (uključujući " << fixed(st.settledAtEndVolume, 2) << " m3 istaloženo na kraju)\n"
            << "Neto promjena terena: " << fixed(st.netLoweredVolume, 2) << " m3 snižen, " << fixed(st.netRaisedVolume, 2)
            << " m3 podignut\n"
            << "Najveća promjena visine: -" << num(st.maxLowering, 4) << " m (erozija) / +" << num(st.maxRaise, 4)
            << " m (taloženje)\n"
            << "Ćelija promijenjeno za više od 1 mm: " << st.cellsChangedMoreThan1mm << "\n"
            << "Greška bilansa vode: " << sci(st.waterBalanceError()) << " m3 (relativno " << sci(st.waterBalanceRelativeError())
            << ")\n"
            << "Greška bilansa materijala: " << sci(st.materialBalanceError) << " m3 (relativno "
            << sci(st.materialBalanceRelativeError()) << ")\n"
            << "Najveća dubina vode / brzina: " << num(st.peakWaterDepth, 4) << " m / " << num(st.peakSpeed, 4) << " m/s\n"
            << "Voda preostala na terenu: " << fixed(st.finalWaterVolume, 2) << " m3, otjecanje preko ruba: "
            << fixed(st.boundaryWaterOutflow, 2) << " m3\n";
        if (!ex.warnings.empty())
        {
            out << "\nUPOZORENJA:\n";
            for (const auto& w : ex.warnings)
                out << "  - " << w << "\n";
        }
        out << "\nFajlovi su zapisani u " << pathToUtf8(std::filesystem::absolute(pathFromUtf8(job.outputDirectory))) << ":\n";
        for (const auto& f : ex.files)
            out << "  " << pathToUtf8(pathFromUtf8(f).filename()) << "\n";
        out << "  simulation_report.txt\n  simulation_report.json\n";
        out << (st.aborted ? "\nSimulacija je PREKINUTA: " + st.abortReason + "\n" : "\nRezultat je uspješno sačuvan.\n");
    }

    // Učitavanje + simulacija + izvoz. Vraća izlazni kod (0 = uspjeh).
    int runJob(Job job, const Heightmap& map, const RasterInfo& info, std::ostream& out)
    {
        if (job.outputDirectory.empty())
            job.outputDirectory = defaultOutputDirectory(job.inputPath);

        out << "\nPokrećem simulaciju (napredak se osvježava svakih nekoliko sekundi; prekid: Ctrl+C)...\n\n";
        std::optional<ErosionSimulation> sim;
        try
        {
            sim.emplace(map, job.config);
            sim->run(consoleProgress(out), 20, 3.0);
        }
        catch (const std::exception& e)
        {
            out << "\nGREŠKA: simulacija nije uspjela: " << e.what() << "\n";
            return 3;
        }
        out << (sim->statistics().aborted ? "\nSimulacija je prekinuta!\n" : "\nSimulacija je završena!\n");

        RunDescription run;
        run.inputPath = job.inputPath;
        run.raster = info;
        run.scale = job.scale;
        run.inverted = job.invert;
        run.config = job.config;
        run.programVersion = kProgramVersion;
        ExportOptions options;
        options.outputDirectory = job.outputDirectory;
        options.baseName = baseNameOf(job.inputPath);
        options.writeCsv = job.writeCsv;
        try
        {
            out << "Upisujem rezultate...\n";
            const ExportResult ex = exportResults(options, run, *sim);
            printResults(out, job, *sim, ex);
        }
        catch (const std::exception& e)
        {
            out << "\nGREŠKA: izvoz rezultata nije uspio: " << e.what() << "\n";
            return 4;
        }
        return sim->statistics().aborted ? 3 : 0;
    }

    // ------------------------------------------------------------------ interaktivni način
    std::optional<std::string> positive(double v)
    {
        if (v <= 0.0)
            return std::string("Vrijednost mora biti veća od nule.");
        return std::nullopt;
    }

    std::optional<std::string> nonNegative(double v)
    {
        if (v < 0.0)
            return std::string("Vrijednost ne može biti negativna.");
        return std::nullopt;
    }

    // Poslije birača terena: sačeka da korisnik preuzme fajlove, pa ponudi najnoviji izvoz iz foldera Preuzimanja.
    // Vraća putanju PNG-a (UTF-8) ako ga je korisnik prihvatio; `eof` postaje true ako je ulaz zatvoren.
    std::optional<std::string> offerLatestExport(Prompter& ask, std::ostream& out, bool& eof)
    {
        out << "\nU pregledniku odaberite područje i kliknite „Generiši teren“, pa „Preuzmi oba fajla“ (.png i .terrain.json).\n";
        if (!ask.readLine("Kad su fajlovi preuzeti, pritisnite Enter:"))
        {
            eof = true;
            return std::nullopt;
        }
        const fs::path folder = downloadsFolder();
        const auto latest = folder.empty() ? std::nullopt : findLatestTerrainExport(folder);
        if (!latest)
        {
            out << "Izvoz nije pronađen u folderu Preuzimanja" << (folder.empty() ? "" : " (" + pathToUtf8(folder) + ")")
                << ". Unesite putanju do PNG-a ručno.\n";
            return std::nullopt;
        }
        const auto age = std::chrono::duration_cast<std::chrono::minutes>(fs::file_time_type::clock::now() - latest->modified);
        out << "\nNajnoviji izvoz iz birača terena: " << pathToUtf8(latest->png) << "\n  (preuzet prije " << std::max<long long>(0, age.count())
            << " min)\n";
        const auto yes = ask.askYesNo("Koristiti ovaj fajl?", true);
        if (!yes)
        {
            eof = true;
            return std::nullopt;
        }
        return *yes ? std::optional<std::string>(pathToUtf8(latest->png)) : std::nullopt;
    }

    // Vraća false ako je korisnik zatvorio ulaz (EOF).
    //   presetPath:     slika koju treba odmah učitati (npr. upravo primljena iz preglednika)
    //   offerDownloads: birač je otvoren kao fajl – ponudi najnoviji izvoz iz foldera Preuzimanja
    bool interactiveSession(Prompter& ask, std::ostream& out, std::optional<std::string> presetPath = std::nullopt,
                            bool offerDownloads = false)
    {
        // 1) height mapa
        Grid2D<double> normalized; // vrijednosti piksela [0, 1]; metri tek kad je poznata skala
        RasterInfo info;
        Job job;
        if (!presetPath && offerDownloads)
        {
            bool eof = false;
            presetPath = offerLatestExport(ask, out, eof);
            if (eof)
                return false;
        }
        while (true)
        {
            std::string path;
            if (presetPath)
            {
                path = *presetPath;
                presetPath.reset();
            }
            else
            {
                const auto line = ask.readLine("\nUnesite putanju do heightmape (.png, .jpg, .jpeg; prazno = povratak na meni):");
                if (!line)
                    return false;
                path = console::stripQuotes(*line);
                if (path.empty())
                    return true;
            }
            out << "\nUčitavam heightmapu...\n";
            try
            {
                LoadedRaster raster = loadRaster(path);
                info = raster.info;
                job.inputPath = path;
                printRasterInfo(out, info);
                normalized = std::move(raster.normalized);
                break;
            }
            catch (const std::exception& e)
            {
                out << "  ! " << e.what() << "\n";
            }
        }

        // 2) skala terena: iz metapodataka birača terena (ako postoje i korisnik ih prihvati), inače ručno
        section(out, "KONFIGURACIJA TERENA");
        bool scaleFromMetadata = false;
        if (const auto meta = findTerrainMetadata(out, job.inputPath, info, {}))
        {
            const auto use = ask.askYesNo("Koristiti ove vrijednosti terena?", true);
            if (!use)
                return false;
            if (*use)
            {
                job.scale = meta->scale;
                job.invert = false; // birač uvijek pravi crno = najniže
                scaleFromMetadata = true;
                out << "Skala terena je preuzeta iz metapodataka (ništa se ne unosi ručno).\n";
            }
        }
        if (!scaleFromMetadata)
        {
            out << "(Slika ne čuva stvarne dimenzije terena; ove vrijednosti su pretpostavke modela.)\n";
            while (true)
            {
                const auto cell = ask.askNumber("Veličina ćelije u metrima (širina terena u metrima / broj piksela)",
                                                TerrainScale::kDefaultCellSizeMeters, positive);
                if (!cell)
                    return false;
                const auto lo = ask.askNumber("Minimalna visina u metrima (crni piksel)", TerrainScale::kDefaultMinElevationMeters);
                if (!lo)
                    return false;
                const auto hi = ask.askNumber("Maksimalna visina u metrima (bijeli piksel)", TerrainScale::kDefaultMaxElevationMeters);
                if (!hi)
                    return false;
                job.scale.cellSizeMeters = *cell;
                job.scale.minElevationMeters = *lo;
                job.scale.maxElevationMeters = *hi;
                const std::string problem = job.scale.validate();
                if (problem.empty())
                    break;
                out << "  ! " << problem << "\n";
            }
            const auto invert = ask.askYesNo("Invertovati heightmapu (bijelo = najniže)?", false);
            if (!invert)
                return false;
            job.invert = *invert;
        }

        // 3) kiša
        section(out, "KONFIGURACIJA KIŠE");
        const auto rain = ask.askNumber("Ukupna količina kiše (L/m2 = mm)", std::nullopt, [](double v) -> std::optional<std::string> {
            return validateRainfallInput(v, 1.0);
        });
        if (!rain)
            return false;
        const auto hours = ask.askNumber("Trajanje kiše (sati)", std::nullopt, [&](double v) -> std::optional<std::string> {
            return validateRainfallInput(*rain, v);
        });
        if (!hours)
            return false;
        job.config.rainfall.totalMillimeters = *rain;
        job.config.rainfall.durationHours = *hours;
        out << "Prosječni intenzitet: " << num(job.config.rainfall.intensityMillimetersPerHour()) << " mm/h\n";
        const auto runoff = ask.askNumber("Period oticanja nakon kiše, bez padavina (sati)", 0.0, nonNegative);
        if (!runoff)
            return false;
        job.config.runoffHours = *runoff;
        const auto boundary = ask.askChoice("Rubni uslov (zatvoren = zidovi, otvoren = voda može napustiti mapu)", "zatvoren",
                                            "otvoren", "zatvoren", "closed", "open");
        if (!boundary)
            return false;
        job.config.flow.boundary = (*boundary == "otvoren") ? BoundaryCondition::Open : BoundaryCondition::Closed;

        const std::string problem = job.config.validate();
        if (!problem.empty())
        {
            out << "  ! " << problem << "\n";
            return true;
        }

        const Heightmap map = makeHeightmap(normalized, job.scale, job.invert, baseNameOf(job.inputPath));
        printSummary(out, job, info);
        const auto start = ask.askYesNo("\nPokrenuti simulaciju?", std::nullopt);
        if (!start)
            return false;
        if (!*start)
        {
            out << "Simulacija je otkazana.\n";
            return true;
        }
        runJob(job, map, info, out);
        return true;
    }

    int interactive()
    {
        Prompter ask(std::cin, std::cout);
        printBanner(std::cout);
        while (true)
        {
            std::cout << "\n[1] Odaberi teren na karti (otvara preglednik)\n[2] Učitaj heightmapu iz fajla\n[3] Izlaz\n\n";
            const auto choice = ask.readLine("Odaberite opciju:");
            if (!choice)
                return 0;
            const std::string c = console::trim(*choice);
            if (c == "1")
            {
                const auto html = findPickerHtml();
                if (!html)
                {
                    std::cout << "  ! Nije pronađen tools/terrain_picker.html (tražen u folderu programa i tekućem folderu).\n"
                                 "    Pokrenite program iz projekta ili odaberite opciju 2.\n";
                    continue;
                }
                bool eof = false, pageOpened = false;
                const auto received = receiveFromBrowser(ask, std::cout, *html, eof, pageOpened);
                if (eof)
                    return 0;
                if (received)
                {
                    if (!interactiveSession(ask, std::cout, *received))
                        return 0;
                    continue;
                }
                // Rezerva: stranica kao lokalni fajl + ručno preuzimanje (server nije pokrenut ili je korisnik odustao).
                if (!pageOpened)
                {
                    if (openInBrowser(*html))
                        std::cout << "\nOtvaram birač terena kao fajl: " << pathToUtf8(*html) << "\n";
                    else
                        std::cout << "\n  ! Preglednik se nije mogao otvoriti automatski. Otvorite ručno fajl:\n    " << pathToUtf8(*html) << "\n";
                }
                if (!interactiveSession(ask, std::cout, std::nullopt, true))
                    return 0;
            }
            else if (c == "2")
            {
                if (!interactiveSession(ask, std::cout))
                    return 0;
            }
            else if (c == "3" || c == "q" || c == "exit" || c == "izlaz")
            {
                std::cout << "Doviđenja.\n";
                return 0;
            }
            else
                std::cout << "  ! Nepoznata opcija \"" << c << "\". Odaberite 1, 2 ili 3.\n";
        }
    }

    // ------------------------------------------------------------------ komandna linija
    void printHelp(std::ostream& out)
    {
        const SimulationConfig d;
        out << kProgramVersion << "\n\n"
            << "Upotreba:\n"
            << "  prs_erosion                          interaktivni meni\n"
            << "  prs_erosion --input FAJL --rain MM --hours H [opcije]\n\n"
            << "Obavezno u načinu komandne linije:\n"
            << "  --input FAJL        heightmapa (.png, .jpg, .jpeg); crno = najniže, bijelo = najviše\n"
            << "  --rain MM           ukupna količina kiše u L/m2 (= mm), >= 0\n"
            << "  --hours H           trajanje kiše u satima, > 0\n\n"
            << "Skala terena (pretpostavke modela; ako uz sliku postoji <ime>.terrain.json iz birača terena,\n"
            << "tools/terrain_picker.html, uzima se iz njega, a zadane opcije --cell/--min/--max imaju prednost):\n"
            << "  --terrain-json F    metapodaci terena iz birača (zadano: <ime slike>.terrain.json pored slike)\n"
            << "  --cell M            veličina ćelije u metrima      [zadano " << TerrainScale::kDefaultCellSizeMeters << "]\n"
            << "  --min M             visina crnog piksela (m)       [zadano " << TerrainScale::kDefaultMinElevationMeters << "]\n"
            << "  --max M             visina bijelog piksela (m)     [zadano " << TerrainScale::kDefaultMaxElevationMeters << "]\n"
            << "  --invert            bijelo = najniže\n\n"
            << "Simulacija:\n"
            << "  --runoff H          period oticanja nakon kiše, bez padavina (sati) [zadano 0]\n"
            << "  --boundary B        closed | open (ili zatvoren | otvoren)  [zadano closed]\n"
            << "  --manning N         Manningov koeficijent hrapavosti n [zadano " << d.flow.manningN << "]\n"
            << "  --infiltration R    brzina infiltracije (mm/h)     [zadano 0]\n"
            << "  --evaporation R     brzina isparavanja (mm/h)      [zadano 0]\n"
            << "  --kc K              koeficijent kapaciteta Kc (s/m) [zadano " << d.erosion.capacityCoefficient << "]\n"
            << "  --ks K              brzina rastvaranja Ks (1/s)    [zadano " << d.erosion.dissolvingRate << "]\n"
            << "  --kd K              brzina taloženja Kd (1/s)      [zadano " << d.erosion.depositionRate << "]\n"
            << "  --max-concentration C  gornja granica koncentracije [zadano " << d.erosion.maxConcentration << "]\n"
            << "  --min-slope S       donja granica sin(alfa)        [zadano " << d.erosion.minimumSlope << "]\n"
            << "  --erodible-depth M  debljina erodibilnog sloja (m), 0 = neograničeno [zadano " << d.erosion.erodibleDepthMeters << "]\n"
            << "  --no-erosion        samo tok vode\n"
            << "  --no-settle         ne taloži preostali suspendovani sediment na kraju\n"
            << "  --courant C         Courantov broj (0, 1]          [zadano " << d.flow.courantNumber << "]\n"
            << "  --max-dt S          najveći vremenski korak (s)    [zadano " << d.flow.maxTimeStepSeconds << "]\n\n"
            << "Izlaz:\n"
            << "  --out FOLDER        izlazni folder                 [zadano output/<ime ulaza>]\n"
            << "  --csv               dodatno upiši erodirane visine kao CSV (17 značajnih cifara)\n"
            << "  --help, --version\n\n"
            << "Izlazni kodovi: 0 uspjeh, 1 neispravni argumenti, 2 heightmapa nije učitana, 3 simulacija nije uspjela/prekinuta, 4 izvoz nije uspio.\n";
    }

    struct CommandLine
    {
        Job job;
        bool help = false;
        bool version = false;
        bool rainGiven = false;
        bool hoursGiven = false;
        bool cellGiven = false, minGiven = false, maxGiven = false; // eksplicitna skala ima prednost nad metapodacima
    };

    // Vraća poruku greške ili prazan string.
    std::string parseCommandLine(const std::vector<std::string>& args, CommandLine& cl)
    {
        Job& job = cl.job;
        const std::map<std::string, double*> numbers = {
            {"--rain", &job.config.rainfall.totalMillimeters},
            {"--hours", &job.config.rainfall.durationHours},
            {"--runoff", &job.config.runoffHours},
            {"--cell", &job.scale.cellSizeMeters},
            {"--min", &job.scale.minElevationMeters},
            {"--max", &job.scale.maxElevationMeters},
            {"--manning", &job.config.flow.manningN},
            {"--infiltration", &job.config.flow.infiltrationMillimetersPerHour},
            {"--evaporation", &job.config.flow.evaporationMillimetersPerHour},
            {"--kc", &job.config.erosion.capacityCoefficient},
            {"--ks", &job.config.erosion.dissolvingRate},
            {"--kd", &job.config.erosion.depositionRate},
            {"--max-concentration", &job.config.erosion.maxConcentration},
            {"--min-slope", &job.config.erosion.minimumSlope},
            {"--erodible-depth", &job.config.erosion.erodibleDepthMeters},
            {"--courant", &job.config.flow.courantNumber},
            {"--max-dt", &job.config.flow.maxTimeStepSeconds},
        };
        for (std::size_t i = 1; i < args.size(); ++i)
        {
            const std::string& a = args[i];
            const auto needValue = [&]() -> std::optional<std::string> {
                if (i + 1 >= args.size())
                    return std::nullopt;
                return args[++i];
            };
            if (a == "--help" || a == "-h" || a == "/?")
                cl.help = true;
            else if (a == "--version")
                cl.version = true;
            else if (a == "--invert")
                job.invert = true;
            else if (a == "--csv")
                job.writeCsv = true;
            else if (a == "--no-erosion")
                job.config.erosion.enabled = false;
            else if (a == "--no-settle")
                job.config.erosion.settleSuspendedSedimentAtEnd = false;
            else if (a == "--input" || a == "--out" || a == "--boundary" || a == "--terrain-json")
            {
                const auto v = needValue();
                if (!v)
                    return "Opcija " + a + " traži vrijednost.";
                if (a == "--input")
                    job.inputPath = console::stripQuotes(*v);
                else if (a == "--out")
                    job.outputDirectory = console::stripQuotes(*v);
                else if (a == "--terrain-json")
                    job.terrainJson = console::stripQuotes(*v);
                else if (*v == "closed" || *v == "zatvoren")
                    job.config.flow.boundary = BoundaryCondition::Closed;
                else if (*v == "open" || *v == "otvoren")
                    job.config.flow.boundary = BoundaryCondition::Open;
                else
                    return "--boundary mora biti closed/zatvoren ili open/otvoren.";
            }
            else if (const auto it = numbers.find(a); it != numbers.end())
            {
                const auto v = needValue();
                if (!v)
                    return "Opcija " + a + " traži brojnu vrijednost.";
                const auto parsed = console::parseNumber(*v);
                if (!parsed)
                    return "Neispravan broj za " + a + ": \"" + *v + "\"";
                *it->second = *parsed;
                cl.rainGiven |= (a == "--rain");
                cl.hoursGiven |= (a == "--hours");
                cl.cellGiven |= (a == "--cell");
                cl.minGiven |= (a == "--min");
                cl.maxGiven |= (a == "--max");
            }
            else
                return "Nepoznata opcija: " + a + " (pogledajte --help)";
        }
        return {};
    }

    int commandLineMode(const std::vector<std::string>& args)
    {
        CommandLine cl;
        const std::string error = parseCommandLine(args, cl);
        if (!error.empty())
        {
            std::cerr << "GREŠKA: " << error << "\n";
            return 1;
        }
        if (cl.help)
        {
            printHelp(std::cout);
            return 0;
        }
        if (cl.version)
        {
            std::cout << kProgramVersion << "\n";
            return 0;
        }
        Job& job = cl.job;
        if (job.inputPath.empty() || !cl.rainGiven || !cl.hoursGiven)
        {
            std::cerr << "GREŠKA: komandna linija traži --input, --rain i --hours (ili pokrenite program bez argumenata).\n";
            return 1;
        }
        if (const auto rainProblem = validateRainfallInput(job.config.rainfall.totalMillimeters, job.config.rainfall.durationHours))
        {
            std::cerr << "GREŠKA: " << *rainProblem << "\n";
            return 1;
        }
        if (const std::string configProblem = job.config.validate(); !configProblem.empty())
        {
            std::cerr << "GREŠKA: " << configProblem << "\n";
            return 1;
        }

        printBanner(std::cout);
        std::cout << "\nUčitavam heightmapu: " << job.inputPath << "\n";
        LoadedRaster raster;
        try
        {
            raster = loadRaster(job.inputPath);
        }
        catch (const std::exception& e)
        {
            std::cerr << "GREŠKA: " << e.what() << "\n";
            return 2;
        }
        const RasterInfo info = raster.info;
        printRasterInfo(std::cout, info);

        // Skala terena: eksplicitne opcije --cell/--min/--max imaju prednost; ostalo dolazi iz metapodataka birača terena
        // (--terrain-json, ili <ime>.terrain.json pored slike kad nijedna od tri opcije nije zadana), inače su zadane vrijednosti.
        const bool anyScaleOption = cl.cellGiven || cl.minGiven || cl.maxGiven;
        if (!job.terrainJson.empty() || !anyScaleOption)
        {
            std::string metaError;
            if (const auto meta = findTerrainMetadata(std::cout, job.inputPath, info, job.terrainJson, &metaError))
            {
                if (!cl.cellGiven)
                    job.scale.cellSizeMeters = meta->scale.cellSizeMeters;
                if (!cl.minGiven)
                    job.scale.minElevationMeters = meta->scale.minElevationMeters;
                if (!cl.maxGiven)
                    job.scale.maxElevationMeters = meta->scale.maxElevationMeters;
                std::cout << "Skala terena je preuzeta iz metapodataka"
                          << ((cl.cellGiven || cl.minGiven || cl.maxGiven) ? " (zadane opcije --cell/--min/--max imaju prednost)" : "")
                          << ".\n";
            }
            else if (!job.terrainJson.empty())
            {
                std::cerr << "GREŠKA: " << metaError << "\n";
                return 1;
            }
        }
        if (const std::string scaleProblem = job.scale.validate(); !scaleProblem.empty())
        {
            std::cerr << "GREŠKA: " << scaleProblem << "\n";
            return 1;
        }
        const Heightmap map = makeHeightmap(raster.normalized, job.scale, job.invert, baseNameOf(job.inputPath));
        printSummary(std::cout, job, info);
        return runJob(job, map, info, std::cout);
    }

    // Argumenti kao UTF-8 (na Windowsu iz UTF-16 komandne linije, da rade i putanje s dijakriticima).
    std::vector<std::string> utf8Arguments(int argc, char** argv)
    {
        std::vector<std::string> args;
#if defined(_WIN32)
        int count = 0;
        LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (wide)
        {
            for (int i = 0; i < count; ++i)
            {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
                std::string s(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0u, '\0');
                if (bytes > 1)
                    WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, s.data(), bytes, nullptr, nullptr);
                args.push_back(std::move(s));
            }
            LocalFree(wide);
            return args;
        }
#endif
        for (int i = 0; i < argc; ++i)
            args.emplace_back(argv[i]);
        return args;
    }
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try
    {
        const std::vector<std::string> args = utf8Arguments(argc, argv);
        if (args.size() > 1)
            return commandLineMode(args);
        return interactive();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Neočekivana greška: " << e.what() << "\n";
        return 5;
    }
}
