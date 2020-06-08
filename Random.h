#pragma once
#include <array>
#include <random>

namespace gt
{

class xorshift128_engine
{
public:
    using result_type = uint64_t;

    static constexpr size_t state_size = 2;
    static constexpr size_t word_size = sizeof(uint64_t);

    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
    static constexpr result_type default_seed = 1;

    explicit xorshift128_engine(uint64_t const seed1, uint64_t const seed2)
        : state_{seed1, seed2}
    {}

    explicit xorshift128_engine()
    {
        std::random_device device;
        state_[0] = device();
        state_[0] |= static_cast<uint64_t>(device()) << 32;
        state_[1] = device();
        state_[1] |= static_cast<uint64_t>(device()) << 32;
    }

    result_type operator()()
    {
        uint64_t a = state_[0];
        uint64_t const b = state_[1];

        uint64_t const result = b + a;
        state_[0] = b;
        a ^= a << 23;
        state_[1] = a ^ b ^ (a >> 18) ^ (b >> 5);

        return result;
    }

private:
    std::array<uint64_t, 2> state_;
};

#define RtlGenRandom SystemFunction036

inline float RandomFloat()
{
    static xorshift128_engine rng;
    static std::uniform_real_distribution<float> dist(0.0f,
                                                      std::nextafter(1.0f, FLT_MAX));
    return dist(rng);
}

inline uint8_t RandomByte()
{
    return RandomFloat() * 255;
}

inline int RandomInt(int const min, int const max)
{
    return static_cast<int>(min + RandomFloat() * (max - min));
}

inline uint32_t RandomColorBGRA()
{
    uint32_t b = RandomByte();
    uint32_t g = RandomByte();
    uint32_t r = RandomByte();
    uint32_t a = RandomByte();
    return b | (g << 8) | (r << 8) | (a << 8);
}

} // namespace gt
  //
