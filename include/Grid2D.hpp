#pragma once
// Grid2D<T> – dvodimenzionalno polje vrijednosti u jednom kontinuiranom nizu (row-major).
//
// Koordinatni sistem je isti kao kod slike: (0, 0) je gornji lijevi piksel, x raste udesno (kolone),
// y raste nadolje (redovi). Indeks ćelije je i = y * width + x, pa su susjedi:
//   lijevo i - 1, desno i + 1, gore i - width, dolje i + width.
//
// Namjerno je jednostavno (bez halo okvira i poravnanja): u ovoj, serijskoj fazi prioritet je čitljivost.
// Kontinuirani niz po polju (Structure of Arrays) je ujedno i raspored koji kasnije olakšava
// paralelizaciju po redovima (OpenMP) i prenos na GPU (jedan bafer po polju).

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace prs
{
    template <typename T>
    class Grid2D
    {
    public:
        Grid2D() = default;

        Grid2D(int width, int height, T value = T{})
        {
            resize(width, height, value);
        }

        void resize(int width, int height, T value = T{})
        {
            if (width <= 0 || height <= 0)
                throw std::invalid_argument("Grid2D: dimenzije moraju biti pozitivne");
            _width = width;
            _height = height;
            _data.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
        }

        int width() const { return _width; }
        int height() const { return _height; }
        std::size_t size() const { return _data.size(); }
        bool empty() const { return _data.empty(); }

        std::size_t index(int x, int y) const
        {
            return static_cast<std::size_t>(y) * static_cast<std::size_t>(_width) + static_cast<std::size_t>(x);
        }

        T& operator()(int x, int y) { return _data[index(x, y)]; }
        const T& operator()(int x, int y) const { return _data[index(x, y)]; }
        T& operator[](std::size_t i) { return _data[i]; }
        const T& operator[](std::size_t i) const { return _data[i]; }

        T* data() { return _data.data(); }
        const T* data() const { return _data.data(); }
        std::vector<T>& values() { return _data; }
        const std::vector<T>& values() const { return _data; }

        void fill(T value) { std::fill(_data.begin(), _data.end(), value); }

        bool sameShape(const Grid2D& other) const
        {
            return _width == other._width && _height == other._height;
        }

        // Zamjena sadržaja u O(1) – koristi se za dvostruko baferovanje (double buffering).
        void swap(Grid2D& other) noexcept
        {
            std::swap(_width, other._width);
            std::swap(_height, other._height);
            _data.swap(other._data);
        }

    private:
        int _width = 0;
        int _height = 0;
        std::vector<T> _data;
    };
}
