#include "cpu_features.hpp"
#include "simulation.hpp"
#include "build_info.hpp"

#include <cstring>
#include <sstream>

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define SW_X86 1
#include <xmmintrin.h>
#if !defined(_MSC_VER)
#include <cpuid.h>
#endif
#endif

#if defined(PRS_HAS_OPENMP)
#include <omp.h>
#endif

namespace sw
{
    namespace
    {
#if defined(SW_X86)
        void cpuid(int leaf, int sub, unsigned regs[4])
        {
#if defined(_MSC_VER)
            int r[4];
            __cpuidex(r, leaf, sub);
            for (int i = 0; i < 4; ++i)
                regs[i] = static_cast<unsigned>(r[i]);
#else
            __cpuid_count(leaf, sub, regs[0], regs[1], regs[2], regs[3]);
#endif
        }

        unsigned long long xgetbv0()
        {
#if defined(_MSC_VER)
            return _xgetbv(0);
#else
            unsigned eax = 0, edx = 0;
            __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
            return (static_cast<unsigned long long>(edx) << 32) | eax;
#endif
        }
#endif
    }

    bool avx2_supported()
    {
#if defined(SW_X86)
        unsigned r[4] = {0, 0, 0, 0};
        cpuid(0, 0, r);
        if (r[0] < 7)
            return false;
        cpuid(1, 0, r);
        const bool osxsave = (r[2] & (1u << 27)) != 0;
        const bool avx = (r[2] & (1u << 28)) != 0;
        if (!osxsave || !avx)
            return false;
        if ((xgetbv0() & 0x6) != 0x6) // OS čuva XMM i YMM registre
            return false;
        cpuid(7, 0, r);
        return (r[1] & (1u << 5)) != 0; // EBX bit 5 = AVX2
#else
        return false;
#endif
    }

    std::string cpu_brand()
    {
#if defined(SW_X86)
        unsigned r[4];
        cpuid(static_cast<int>(0x80000000u), 0, r);
        if (r[0] < 0x80000004u)
            return {};
        char brand[49] = {};
        for (unsigned leaf = 0; leaf < 3; ++leaf)
        {
            cpuid(static_cast<int>(0x80000002u + leaf), 0, r);
            std::memcpy(brand + leaf * 16, r, 16);
        }
        std::string s(brand);
        const auto first = s.find_first_not_of(' ');
        return first == std::string::npos ? std::string{} : s.substr(first);
#else
        return {};
#endif
    }

    void enable_flush_denormals()
    {
#if defined(SW_X86)
        // FTZ (bit 15) i DAZ (bit 6) u MXCSR registru tekuće dretve. Utiče samo na vrijednosti
        // manje od ~1.18e-38, ali sprečava višestruko usporenje zbog subnormalnih brojeva
        // (npr. sediment koji eksponencijalno opada u suhim ćelijama).
        _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
    }

    std::string build_description()
    {
        std::ostringstream os;
        os << "compiler=" << PRS_BUILD_COMPILER << "; build_type=" << PRS_BUILD_TYPE
           << "; cxx_flags=" << PRS_BUILD_CXX_FLAGS << "; scalar_flags=" << PRS_BUILD_SCALAR_FLAGS
           << "; vector_flags=" << PRS_BUILD_VECTOR_FLAGS;
#if defined(PRS_HAS_OPENMP)
        os << "; openmp=" << _OPENMP << " (max_threads=" << omp_get_max_threads() << ")";
#else
        os << "; openmp=off";
#endif
#if defined(PRS_HAS_AVX2_BUILD)
        os << "; avx2_build=on";
#else
        os << "; avx2_build=off";
#endif
        return os.str();
    }
}
