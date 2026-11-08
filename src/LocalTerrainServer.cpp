#include "LocalTerrainServer.hpp"

#include "TerrainMetadata.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalid = INVALID_SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalid = -1;
static void closeSocket(socket_t s) { ::close(s); }
#endif

namespace prs
{
    namespace
    {
        constexpr std::size_t kMaxHeader = 16 * 1024;
        constexpr std::size_t kMaxBody = 256u * 1024u * 1024u; // 256 MB – i 4096x4096 16-bitni PNG je manji

        socket_t toSocket(std::intptr_t s) { return static_cast<socket_t>(s); }

        bool initSockets()
        {
#if defined(_WIN32)
            static const bool ok = [] {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            return ok;
#else
            return true;
#endif
        }

        void setReceiveTimeout(socket_t s, int milliseconds)
        {
#if defined(_WIN32)
            const DWORD ms = static_cast<DWORD>(milliseconds);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
            timeval tv{};
            tv.tv_sec = milliseconds / 1000;
            tv.tv_usec = (milliseconds % 1000) * 1000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        }

        bool sendAll(socket_t s, const char* data, std::size_t size)
        {
            while (size > 0)
            {
                const int chunk = static_cast<int>(std::min<std::size_t>(size, 1 << 20));
#if defined(MSG_NOSIGNAL)
                const auto n = ::send(s, data, chunk, MSG_NOSIGNAL); // preglednik zatvorio vezu: bez SIGPIPE
#else
                const auto n = ::send(s, data, chunk, 0);
#endif
                if (n <= 0)
                    return false;
                data += n;
                size -= static_cast<std::size_t>(n);
            }
            return true;
        }

        void respond(socket_t s, int status, const std::string& reason, const std::string& contentType, const std::string& body)
        {
            std::ostringstream h;
            h << "HTTP/1.1 " << status << " " << reason << "\r\n"
              << "Content-Type: " << contentType << "\r\n"
              << "Content-Length: " << body.size() << "\r\n"
              << "Cache-Control: no-store\r\n"
              << "X-Content-Type-Options: nosniff\r\n"
              << "Connection: close\r\n\r\n";
            const std::string head = h.str();
            sendAll(s, head.data(), head.size());
            sendAll(s, body.data(), body.size());
        }

        void respondJson(socket_t s, int status, const std::string& reason, bool ok, const std::string& message)
        {
            std::string escaped;
            for (char c : message)
            {
                if (c == '"' || c == '\\')
                    escaped += '\\';
                if (static_cast<unsigned char>(c) >= 0x20)
                    escaped += c;
            }
            respond(s, status, reason, "application/json; charset=utf-8",
                    std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + escaped + "\"}");
        }

        std::string lower(std::string t)
        {
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return t;
        }

        std::string urlDecode(const std::string& s)
        {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(s[i + 2])))
                {
                    out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                }
                else
                    out += (s[i] == '+') ? ' ' : s[i];
            }
            return out;
        }

        std::string queryParam(const std::string& query, const std::string& key)
        {
            std::size_t pos = 0;
            while (pos <= query.size())
            {
                const std::size_t amp = query.find('&', pos);
                const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
                const std::size_t eq = part.find('=');
                if (part.substr(0, eq) == key)
                    return eq == std::string::npos ? std::string{} : urlDecode(part.substr(eq + 1));
                if (amp == std::string::npos)
                    break;
                pos = amp + 1;
            }
            return {};
        }

        std::string randomToken()
        {
            std::random_device rd;
            static const char* hex = "0123456789abcdef";
            std::string t;
            for (int i = 0; i < 32; ++i)
                t += hex[rd() % 16];
            return t;
        }

        struct Request
        {
            std::string method, path, query, host;
            std::string body;
        };

        // Čita jedan HTTP zahtjev; vraća false ako je neispravan ili prevelik (`error` = razlog).
        bool readRequest(socket_t s, Request& r, std::string& error)
        {
            std::string data;
            char buffer[64 * 1024];
            std::size_t headerEnd = std::string::npos;
            while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos)
            {
                if (data.size() > kMaxHeader)
                    return error = "zaglavlje je preveliko", false;
                const auto n = ::recv(s, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (n <= 0)
                    return error = "veza je prekinuta", false;
                data.append(buffer, static_cast<std::size_t>(n));
            }
            std::istringstream head(data.substr(0, headerEnd));
            std::string line, target, version;
            std::getline(head, line);
            std::istringstream first(line);
            first >> r.method >> target >> version;
            if (r.method.empty() || target.empty())
                return error = "neispravan zahtjev", false;
            const std::size_t q = target.find('?');
            r.path = target.substr(0, q);
            r.query = q == std::string::npos ? std::string{} : target.substr(q + 1);
            std::size_t contentLength = 0;
            while (std::getline(head, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                const std::string name = lower(line.substr(0, colon));
                std::string value = line.substr(colon + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                if (name == "content-length")
                {
                    try
                    {
                        contentLength = static_cast<std::size_t>(std::stoull(value));
                    }
                    catch (...)
                    {
                        return error = "neispravan Content-Length", false;
                    }
                }
                else if (name == "host")
                    r.host = lower(value);
            }
            if (contentLength > kMaxBody)
                return error = "tijelo zahtjeva je preveliko", false;
            r.body = data.substr(headerEnd + 4);
            while (r.body.size() < contentLength)
            {
                const auto n = ::recv(s, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (n <= 0)
                    return error = "tijelo zahtjeva nije potpuno", false;
                r.body.append(buffer, static_cast<std::size_t>(n));
            }
            r.body.resize(contentLength);
            return true;
        }
    }

    std::string sanitizeTerrainName(const std::string& name)
    {
        std::string out;
        for (char c : name)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')
                out += c;
        while (!out.empty() && out.front() == '.')
            out.erase(0, 1);
        if (out.size() > 120)
            out.resize(120);
        return out.empty() ? std::string("teren") : out;
    }

    TerrainReceiver::~TerrainReceiver()
    {
        stop();
    }

    std::string TerrainReceiver::start(const std::filesystem::path& htmlFile, int port)
    {
        stop();
        if (!initSockets())
            return "mrežni podsistem (Winsock) se nije mogao pokrenuti";
        const socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kInvalid)
            return "ne mogu napraviti socket";
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // samo 127.0.0.1 – nije dostupno s mreže
        addr.sin_port = htons(static_cast<unsigned short>(port));
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, 8) != 0)
        {
            closeSocket(s);
            return "ne mogu otvoriti lokalni port";
        }
        socklen_t len = sizeof(addr);
        ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
        _listen = static_cast<std::intptr_t>(s);
        _port = ntohs(addr.sin_port);
        _token = randomToken();
        _html = htmlFile;
        _name.clear();
        _png.clear();
        _json.clear();
        return {};
    }

    void TerrainReceiver::stop()
    {
        if (_listen >= 0)
            closeSocket(toSocket(_listen));
        _listen = -1;
    }

    std::string TerrainReceiver::url() const
    {
        return "http://127.0.0.1:" + std::to_string(_port) + "/?token=" + _token;
    }

    ReceivedTerrain TerrainReceiver::take()
    {
        ReceivedTerrain r{_name.empty() ? std::string("teren") : _name, std::move(_png), std::move(_json)};
        _png.clear();
        _json.clear();
        _name.clear();
        return r;
    }

    bool TerrainReceiver::poll(int timeoutMs)
    {
        if (_listen < 0)
            return false;
        const socket_t ls = toSocket(_listen);
        fd_set set;
        FD_ZERO(&set);
        FD_SET(ls, &set);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
#if defined(_WIN32)
        const int ready = ::select(0, &set, nullptr, nullptr, &tv);
#else
        const int ready = ::select(ls + 1, &set, nullptr, nullptr, &tv);
#endif
        if (ready <= 0)
            return false;
        const socket_t c = ::accept(ls, nullptr, nullptr);
        if (c == kInvalid)
            return false;
        setReceiveTimeout(c, 10000);

        Request r;
        std::string error;
        if (!readRequest(c, r, error))
        {
            _lastMessage = "odbijen zahtjev: " + error;
            respondJson(c, 400, "Bad Request", false, error);
            closeSocket(c);
            return true;
        }
        // Host mora biti lokalni (zaštita od DNS rebinding napada: tuđa stranica ne može glumiti 127.0.0.1).
        const std::string p = ":" + std::to_string(_port);
        if (r.host != "127.0.0.1" + p && r.host != "localhost" + p)
        {
            _lastMessage = "odbijen zahtjev: Host \"" + r.host + "\" nije lokalni";
            respondJson(c, 403, "Forbidden", false, "Host nije dozvoljen");
            closeSocket(c);
            return true;
        }

        if (r.method == "GET" && (r.path == "/" || r.path == "/terrain_picker.html"))
        {
            std::ifstream in(_html, std::ios::binary);
            std::ostringstream content;
            content << in.rdbuf();
            if (!in)
                respondJson(c, 500, "Internal Server Error", false, "stranica birača nije pronađena");
            else
                respond(c, 200, "OK", "text/html; charset=utf-8", content.str());
        }
        else if (r.method == "GET" && r.path == "/status")
        {
            respondJson(c, 200, "OK", true, complete() ? "primljeno" : "čekam");
        }
        else if (r.method == "POST" && (r.path == "/upload/png" || r.path == "/upload/json"))
        {
            if (queryParam(r.query, "token") != _token)
            {
                _lastMessage = "odbijen upload: pogrešan token";
                respondJson(c, 403, "Forbidden", false, "pogrešan token – otvorite stranicu ponovo iz programa");
            }
            else if (r.path == "/upload/png")
            {
                static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
                if (r.body.size() < 8 || std::memcmp(r.body.data(), signature, 8) != 0)
                {
                    _lastMessage = "odbijen upload: tijelo nije PNG";
                    respondJson(c, 400, "Bad Request", false, "tijelo nije PNG slika");
                }
                else
                {
                    _png.assign(r.body.begin(), r.body.end());
                    _name = sanitizeTerrainName(queryParam(r.query, "name"));
                    respondJson(c, 200, "OK", true, "PNG primljen");
                }
            }
            else
            {
                try
                {
                    parseTerrainMetadata(r.body); // provjera prije prihvatanja
                    _json = r.body;
                    respondJson(c, 200, "OK", true, "metapodaci primljeni");
                }
                catch (const std::exception& e)
                {
                    _lastMessage = std::string("odbijeni metapodaci: ") + e.what();
                    respondJson(c, 400, "Bad Request", false, e.what());
                }
            }
        }
        else
            respondJson(c, 404, "Not Found", false, "nepoznata putanja");
        closeSocket(c);
        return true;
    }
}
