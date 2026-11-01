#pragma once
// Poravnati niz float vrijednosti (proširenje klase AlignedFloats iz simd_support.hpp sa vježbi):
// 64-bajtno poravnanje (linija keša, >= 32 B potrebnih za _mm256_load_ps), move semantika,
// inicijalizacija nulama i kopiranje stanja.

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace sw
{
    class AlignedBuffer
    {
    public:
        static constexpr std::size_t alignment = 64;

        AlignedBuffer() = default;
        explicit AlignedBuffer(std::size_t count) { allocate(count); }
        ~AlignedBuffer() { release(); }

        AlignedBuffer(const AlignedBuffer&) = delete;
        AlignedBuffer& operator=(const AlignedBuffer&) = delete;

        AlignedBuffer(AlignedBuffer&& other) noexcept
            : _data(std::exchange(other._data, nullptr)), _size(std::exchange(other._size, 0))
        {
        }

        AlignedBuffer& operator=(AlignedBuffer&& other) noexcept
        {
            if (this != &other)
            {
                release();
                _data = std::exchange(other._data, nullptr);
                _size = std::exchange(other._size, 0);
            }
            return *this;
        }

        void allocate(std::size_t count)
        {
            release();
            std::size_t bytes = count * sizeof(float);
            bytes = (bytes + alignment - 1) / alignment * alignment; // aligned_alloc traži višekratnik
            if (bytes == 0)
                bytes = alignment;
#if defined(_MSC_VER)
            _data = static_cast<float*>(_aligned_malloc(bytes, alignment));
#else
            _data = static_cast<float*>(std::aligned_alloc(alignment, bytes));
#endif
            if (!_data)
                throw std::bad_alloc();
            std::memset(_data, 0, bytes);
            _size = count;
        }

        void copy_from(const AlignedBuffer& other)
        {
            if (other._size != _size)
                allocate(other._size);
            std::memcpy(_data, other._data, _size * sizeof(float));
        }

        float* data() noexcept { return _data; }
        const float* data() const noexcept { return _data; }
        std::size_t size() const noexcept { return _size; }

    private:
        void release() noexcept
        {
            if (_data)
            {
#if defined(_MSC_VER)
                _aligned_free(_data);
#else
                std::free(_data);
#endif
            }
            _data = nullptr;
            _size = 0;
        }

        float* _data = nullptr;
        std::size_t _size = 0;
    };
}
