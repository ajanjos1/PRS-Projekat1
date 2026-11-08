#pragma once
// Pomoćne funkcije za konzolni unos: parsiranje brojeva i pitanja s podrazumijevanim vrijednostima.
//
// Rade nad std::istream / std::ostream (ne direktno nad std::cin), pa se mogu testirati sa std::stringstream.
// Neispravan unos (tekst, NaN, Inf, prazno bez podrazumijevane vrijednosti, broj izvan dozvoljenog raspona)
// nikad ne ruši program: ispisuje se razlog i pitanje se ponavlja. Kraj ulaza (EOF, Ctrl+Z / Ctrl+D) vraća
// std::nullopt, pa pozivalac može uredno izaći.

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

namespace prs::console
{
    std::string trim(const std::string& text);

    // Uklanja navodnike oko putanje ("C:\a b\x.png" -> C:\a b\x.png), npr. iz Windows "Copy as path".
    std::string stripQuotes(const std::string& text);

    // Strogo parsiranje decimalnog broja: dozvoljen je i decimalni zarez ("2,5"). Odbija prazan unos,
    // višak znakova ("12abc"), NaN i beskonačnost. Ne zavisi od lokalnih postavki (std::from_chars).
    std::optional<double> parseNumber(const std::string& text);

    // Validator vraća poruku greške ili nullopt ako je vrijednost prihvatljiva.
    using NumberValidator = std::function<std::optional<std::string>(double)>;

    class Prompter
    {
    public:
        Prompter(std::istream& in, std::ostream& out);

        // Ispiše label i učita jednu liniju; nullopt na EOF.
        std::optional<std::string> readLine(const std::string& label);

        // Broj s opcionom podrazumijevanom vrijednošću (prazan unos = default). Ponavlja dok unos nije ispravan.
        std::optional<double> askNumber(const std::string& label, std::optional<double> defaultValue,
                                        const NumberValidator& validator = {});

        // Da/ne pitanje: y/yes/d/da ili n/no/ne (bez obzira na velika slova); prazno = default.
        std::optional<bool> askYesNo(const std::string& label, std::optional<bool> defaultValue);

        // Izbor jedne od ponuđenih riječi (npr. "closed"/"open"); prazno = default.
        // Vraća jednu od vrijednosti `first` / `second`. firstAlias / secondAlias su dodatni dozvoljeni unosi
        // (npr. engleski naziv) koji se mapiraju na istu vrijednost.
        std::optional<std::string> askChoice(const std::string& label, const std::string& first,
                                             const std::string& second, const std::string& defaultValue,
                                             const std::string& firstAlias = {}, const std::string& secondAlias = {});

    private:
        std::istream& _in;
        std::ostream& _out;
    };
}
