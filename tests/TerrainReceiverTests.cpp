// Lokalni server za birač terena: stranica se servira, upload radi samo s tokenom i lokalnim Host zaglavljem,
// neispravni podaci se odbijaju. Klijent je jednostavan socket u istom procesu: zahtjev se pošalje u bafer
// socketa, pa poll() servera obradi vezu (bez dretvi).

#include "LocalTerrainServer.hpp"
#include "TestFramework.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using client_socket = SOCKET;
static void closeClient(client_socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using client_socket = int;
static void closeClient(client_socket s) { ::close(s); }
#endif

using namespace prs;
using namespace prs::test;
namespace fs = std::filesystem;

namespace
{
    // Pošalje zahtjev, pusti server da ga obradi i vrati cijeli odgovor.
    std::string httpExchange(TerrainReceiver& server, const std::string& request)
    {
        const client_socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<unsigned short>(server.port()));
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            closeClient(s);
            return "CONNECT FAILED";
        }
        ::send(s, request.data(), static_cast<int>(request.size()), 0);
        server.poll(2000);
        std::string response;
        char buffer[4096];
        for (;;)
        {
            const auto n = ::recv(s, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (n <= 0)
                break;
            response.append(buffer, static_cast<std::size_t>(n));
        }
        closeClient(s);
        return response;
    }

    std::string post(TerrainReceiver& server, const std::string& target, const std::string& body,
                     const std::string& host = "")
    {
        const std::string h = host.empty() ? "127.0.0.1:" + std::to_string(server.port()) : host;
        return httpExchange(server, "POST " + target + " HTTP/1.1\r\nHost: " + h + "\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                                    std::to_string(body.size()) + "\r\n\r\n" + body);
    }

    bool startsWith(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

    const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "ostatak-slike";
    const std::string kJson = R"({"formatVersion":1,"cellSizeMeters":11.3,"minElevationMeters":1000,"maxElevationMeters":2058})";
}

PRS_TEST("TerrainReceiver", stranica_upload_i_zastita)
{
    const fs::path html = fs::temp_directory_path() / "prs_receiver_test.html";
    std::ofstream(html, std::ios::binary) << "<!doctype html><title>birac</title>";

    TerrainReceiver server;
    const std::string error = server.start(html);
    CHECK(error.empty());
    if (!error.empty())
    {
        note("server nije pokrenut: " + error);
        return;
    }
    CHECK(server.port() > 0);
    CHECK(server.token().size() == 32);
    CHECK(server.url() == "http://127.0.0.1:" + std::to_string(server.port()) + "/?token=" + server.token());
    const std::string host = "127.0.0.1:" + std::to_string(server.port());
    const std::string tok = "token=" + server.token();

    // stranica
    const std::string page = httpExchange(server, "GET /?" + tok + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
    CHECK(startsWith(page, "HTTP/1.1 200"));
    CHECK(page.find("<title>birac</title>") != std::string::npos);
    CHECK(page.find("text/html") != std::string::npos);

    // nepoznata putanja, pogrešan token, tuđi Host (DNS rebinding), tijelo koje nije PNG, neispravan JSON
    CHECK(startsWith(httpExchange(server, "GET /nema HTTP/1.1\r\nHost: " + host + "\r\n\r\n"), "HTTP/1.1 404"));
    CHECK(startsWith(post(server, "/upload/png?token=pogresan&name=x", kPng), "HTTP/1.1 403"));
    CHECK(startsWith(post(server, "/upload/png?" + tok + "&name=x", kPng, "napadac.example:" + std::to_string(server.port())),
                     "HTTP/1.1 403"));
    CHECK(startsWith(post(server, "/upload/png?" + tok + "&name=x", "nije png"), "HTTP/1.1 400"));
    CHECK(startsWith(post(server, "/upload/json?" + tok, R"({"cellSizeMeters":0})"), "HTTP/1.1 400"));
    CHECK(!server.complete());

    // ispravan prijenos (localhost je takođe dozvoljen Host)
    CHECK(startsWith(post(server, "/upload/png?" + tok + "&name=..%2F..%2Fteren_N43.7%20x", kPng), "HTTP/1.1 200"));
    CHECK(!server.complete()); // fali JSON
    CHECK(startsWith(post(server, "/upload/json?" + tok, kJson, "localhost:" + std::to_string(server.port())), "HTTP/1.1 200"));
    CHECK(server.complete());
    const ReceivedTerrain r = server.take();
    CHECK(r.name == "teren_N43.7x"); // "../" i razmak uklonjeni – nema izlaska iz foldera
    CHECK(std::string(r.png.begin(), r.png.end()) == kPng);
    CHECK(r.json == kJson);
    CHECK(!server.complete());

    server.stop();
    CHECK(!server.running());
    std::error_code ec;
    fs::remove(html, ec);
}

PRS_TEST("TerrainReceiver", ciscenje_imena)
{
    CHECK(sanitizeTerrainName("teren_N43.707_E018.266_512x512") == "teren_N43.707_E018.266_512x512");
    CHECK(sanitizeTerrainName("../../windows/system32") == "windowssystem32");
    CHECK(sanitizeTerrainName("") == "teren");
    CHECK(sanitizeTerrainName("...") == "teren");
    CHECK(sanitizeTerrainName(std::string(300, 'a')).size() == 120);
}
