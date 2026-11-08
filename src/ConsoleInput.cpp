#include "ConsoleInput.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <istream>
#include <ostream>

namespace prs::console
{
    std::string trim(const std::string& text)
    {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        const auto first = std::find_if(text.begin(), text.end(), notSpace);
        const auto last = std::find_if(text.rbegin(), text.rend(), notSpace).base();
        return first < last ? std::string(first, last) : std::string{};
    }

    std::string stripQuotes(const std::string& text)
    {
        std::string t = trim(text);
        if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') || (t.front() == '\'' && t.back() == '\'')))
            t = t.substr(1, t.size() - 2);
        return t;
    }

    std::optional<double> parseNumber(const std::string& text)
    {
        std::string t = trim(text);
        if (t.empty())
            return std::nullopt;
        std::replace(t.begin(), t.end(), ',', '.'); // "2,5" -> "2.5"
        if (t.front() == '+')
            t.erase(0, 1);
        double value = 0.0;
        const char* begin = t.data();
        const char* end = t.data() + t.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value, std::chars_format::general);
        if (ec != std::errc{} || ptr != end || !std::isfinite(value))
            return std::nullopt;
        return value;
    }

    Prompter::Prompter(std::istream& in, std::ostream& out) : _in(in), _out(out) {}

    std::optional<std::string> Prompter::readLine(const std::string& label)
    {
        _out << label << "\n> " << std::flush;
        std::string line;
        if (!std::getline(_in, line))
        {
            _out << "\n";
            return std::nullopt;
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return line;
    }

    std::optional<double> Prompter::askNumber(const std::string& label, std::optional<double> defaultValue,
                                              const NumberValidator& validator)
    {
        std::string fullLabel = label;
        if (defaultValue)
        {
            std::string d = std::to_string(*defaultValue);
            // 10.000000 -> 10, 0.500000 -> 0.5
            if (d.find('.') != std::string::npos)
            {
                d.erase(d.find_last_not_of('0') + 1);
                if (!d.empty() && d.back() == '.')
                    d.pop_back();
            }
            fullLabel += " [zadano: " + d + "]";
        }
        fullLabel += ":";
        while (true)
        {
            const auto line = readLine(fullLabel);
            if (!line)
                return std::nullopt;
            double value;
            if (trim(*line).empty())
            {
                if (!defaultValue)
                {
                    _out << "  ! Unesite broj.\n";
                    continue;
                }
                value = *defaultValue;
            }
            else
            {
                const auto parsed = parseNumber(*line);
                if (!parsed)
                {
                    _out << "  ! \"" << trim(*line) << "\" nije ispravan broj (primjer: 12.5 ili 12,5).\n";
                    continue;
                }
                value = *parsed;
            }
            if (validator)
            {
                if (const auto problem = validator(value))
                {
                    _out << "  ! " << *problem << "\n";
                    continue;
                }
            }
            return value;
        }
    }

    std::optional<bool> Prompter::askYesNo(const std::string& label, std::optional<bool> defaultValue)
    {
        std::string fullLabel = label + (defaultValue ? (*defaultValue ? " [D/n]:" : " [d/N]:") : " [D/N]:");
        while (true)
        {
            const auto line = readLine(fullLabel);
            if (!line)
                return std::nullopt;
            std::string t = trim(*line);
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (t.empty() && defaultValue)
                return *defaultValue;
            if (t == "y" || t == "yes" || t == "d" || t == "da")
                return true;
            if (t == "n" || t == "no" || t == "ne")
                return false;
            _out << "  ! Odgovorite sa D (da) ili N (ne).\n";
        }
    }

    std::optional<std::string> Prompter::askChoice(const std::string& label, const std::string& first,
                                                   const std::string& second, const std::string& defaultValue,
                                                   const std::string& firstAlias, const std::string& secondAlias)
    {
        const std::string fullLabel = label + " [" + first + "/" + second + ", zadano: " + defaultValue + "]:";
        while (true)
        {
            const auto line = readLine(fullLabel);
            if (!line)
                return std::nullopt;
            std::string t = trim(*line);
            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (t.empty())
                return defaultValue;
            if (t == first || (!firstAlias.empty() && t == firstAlias))
                return first;
            if (t == second || (!secondAlias.empty() && t == secondAlias))
                return second;
            _out << "  ! Dozvoljeno: " << first << " ili " << second << ".\n";
        }
    }
}
