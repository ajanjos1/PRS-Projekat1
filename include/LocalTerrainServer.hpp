#pragma once
// TerrainReceiver – mali lokalni HTTP server preko kojeg birač terena (tools/terrain_picker.html) šalje
// height mapu i metapodatke direktno programu, bez ručnog preuzimanja fajlova.
//
//   1. start(html): sluša samo na 127.0.0.1 (nije dostupan s mreže), port bira operativni sistem.
//   2. Program otvori url() u pregledniku: http://127.0.0.1:<port>/?token=<nasumični token>.
//      GET /  vraća stranicu birača; ona prepozna token i nakon „Generiši teren“ pošalje:
//      POST /upload/png?token=..&name=..   (tijelo = 16-bitni PNG)
//      POST /upload/json?token=..          (tijelo = .terrain.json)
//   3. Program u petlji zove poll(); kad complete() postane true, take() vraća primljene podatke.
//
// Zaštita: upload bez ispravnog tokena se odbija (403), kao i zahtjev čije Host zaglavlje nije 127.0.0.1/localhost
// (zaštita od DNS rebinding napada). Server je namjerno jednostavan: obrađuje jedan zahtjev po pozivu poll(),
// bez dretvi (u skladu s ovom, serijskom fazom projekta).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prs
{
    struct ReceivedTerrain
    {
        std::string name;               // osnovno ime fajla (npr. teren_N43.707_E018.266_512x512), očišćeno
        std::vector<std::uint8_t> png;  // 16-bitni PNG
        std::string json;               // sadržaj .terrain.json (već provjeren parserom metapodataka)
    };

    class TerrainReceiver
    {
    public:
        TerrainReceiver() = default;
        ~TerrainReceiver();
        TerrainReceiver(const TerrainReceiver&) = delete;
        TerrainReceiver& operator=(const TerrainReceiver&) = delete;

        // Pokreće server. port 0 = slobodan port po izboru sistema. Vraća prazan string ili opis greške.
        std::string start(const std::filesystem::path& htmlFile, int port = 0);
        void stop();
        bool running() const { return _listen >= 0; }

        int port() const { return _port; }
        const std::string& token() const { return _token; }
        std::string url() const;

        // Čeka najviše timeoutMs na jedan zahtjev i obradi ga. Vraća true ako je obrađen neki zahtjev.
        bool poll(int timeoutMs);

        bool complete() const { return !_png.empty() && !_json.empty(); }
        ReceivedTerrain take();

        // Posljednja poruka o odbijenom zahtjevu (za ispis/testove).
        const std::string& lastMessage() const { return _lastMessage; }

    private:
        std::intptr_t _listen = -1;
        int _port = 0;
        std::string _token;
        std::filesystem::path _html;
        std::string _name;
        std::vector<std::uint8_t> _png;
        std::string _json;
        std::string _lastMessage;
    };

    // Ime fajla iz preglednika svedeno na [A-Za-z0-9._-] (najviše 120 znakova); prazno -> "teren".
    std::string sanitizeTerrainName(const std::string& name);
}
