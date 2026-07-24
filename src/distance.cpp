#include "fbvector/distance.h"
#include <cmath>

#undef HWY_TARGET_TOGGLES
#define HWY_SHARED_POINTERS

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace fbvector {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Euclidean (L2) distance squared helper using SIMD
float L2DistanceSquaredSIMD(const float* a, const float* b, size_t size) {
    const hn::ScalableTag<float> d;
    const size_t lanes = hn::Lanes(d);
    auto sum0 = hn::Zero(d);
    auto sum1 = hn::Zero(d);
    auto sum2 = hn::Zero(d);
    auto sum3 = hn::Zero(d);
    
    size_t i = 0;
    const size_t unrolled_lanes = lanes * 4;
    const size_t rounded_size = (size / unrolled_lanes) * unrolled_lanes;
    for (; i < rounded_size; i += unrolled_lanes) {
        const auto va0 = hn::LoadU(d, a + i);
        const auto vb0 = hn::LoadU(d, b + i);
        const auto diff0 = hn::Sub(va0, vb0);
        sum0 = hn::MulAdd(diff0, diff0, sum0);

        const auto va1 = hn::LoadU(d, a + i + lanes);
        const auto vb1 = hn::LoadU(d, b + i + lanes);
        const auto diff1 = hn::Sub(va1, vb1);
        sum1 = hn::MulAdd(diff1, diff1, sum1);

        const auto va2 = hn::LoadU(d, a + i + 2 * lanes);
        const auto vb2 = hn::LoadU(d, b + i + 2 * lanes);
        const auto diff2 = hn::Sub(va2, vb2);
        sum2 = hn::MulAdd(diff2, diff2, sum2);

        const auto va3 = hn::LoadU(d, a + i + 3 * lanes);
        const auto vb3 = hn::LoadU(d, b + i + 3 * lanes);
        const auto diff3 = hn::Sub(va3, vb3);
        sum3 = hn::MulAdd(diff3, diff3, sum3);
    }
    
    const size_t rounded_size_single = (size / lanes) * lanes;
    for (; i < rounded_size_single; i += lanes) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        const auto diff = hn::Sub(va, vb);
        sum0 = hn::MulAdd(diff, diff, sum0);
    }
    
    auto sum_vec = hn::Add(hn::Add(sum0, sum1), hn::Add(sum2, sum3));
    float sum = hn::GetLane(hn::SumOfLanes(d, sum_vec));
    for (; i < size; ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
}

// Dot product helper using SIMD
float DotProductSIMD(const float* a, const float* b, size_t size) {
    const hn::ScalableTag<float> d;
    const size_t lanes = hn::Lanes(d);
    auto sum0 = hn::Zero(d);
    auto sum1 = hn::Zero(d);
    auto sum2 = hn::Zero(d);
    auto sum3 = hn::Zero(d);
    
    size_t i = 0;
    const size_t unrolled_lanes = lanes * 4;
    const size_t rounded_size = (size / unrolled_lanes) * unrolled_lanes;
    for (; i < rounded_size; i += unrolled_lanes) {
        const auto va0 = hn::LoadU(d, a + i);
        const auto vb0 = hn::LoadU(d, b + i);
        sum0 = hn::MulAdd(va0, vb0, sum0);

        const auto va1 = hn::LoadU(d, a + i + lanes);
        const auto vb1 = hn::LoadU(d, b + i + lanes);
        sum1 = hn::MulAdd(va1, vb1, sum1);

        const auto va2 = hn::LoadU(d, a + i + 2 * lanes);
        const auto vb2 = hn::LoadU(d, b + i + 2 * lanes);
        sum2 = hn::MulAdd(va2, vb2, sum2);

        const auto va3 = hn::LoadU(d, a + i + 3 * lanes);
        const auto vb3 = hn::LoadU(d, b + i + 3 * lanes);
        sum3 = hn::MulAdd(va3, vb3, sum3);
    }
    
    const size_t rounded_size_single = (size / lanes) * lanes;
    for (; i < rounded_size_single; i += lanes) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        sum0 = hn::MulAdd(va, vb, sum0);
    }
    
    auto sum_vec = hn::Add(hn::Add(sum0, sum1), hn::Add(sum2, sum3));
    float sum = hn::GetLane(hn::SumOfLanes(d, sum_vec));
    for (; i < size; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

// Cosine similarity helper using SIMD to calculate dot, norm_a, norm_b in a single pass
void CosinePartsSIMD(const float* a, const float* b, size_t size, float& dot, float& norm_a, float& norm_b) {
    const hn::ScalableTag<float> d;
    const size_t lanes = hn::Lanes(d);
    
    auto dot0 = hn::Zero(d);
    auto dot1 = hn::Zero(d);
    auto dot2 = hn::Zero(d);
    auto dot3 = hn::Zero(d);

    auto norm_a0 = hn::Zero(d);
    auto norm_a1 = hn::Zero(d);
    auto norm_a2 = hn::Zero(d);
    auto norm_a3 = hn::Zero(d);

    auto norm_b0 = hn::Zero(d);
    auto norm_b1 = hn::Zero(d);
    auto norm_b2 = hn::Zero(d);
    auto norm_b3 = hn::Zero(d);
    
    size_t i = 0;
    const size_t unrolled_lanes = lanes * 4;
    const size_t rounded_size = (size / unrolled_lanes) * unrolled_lanes;
    for (; i < rounded_size; i += unrolled_lanes) {
        const auto va0 = hn::LoadU(d, a + i);
        const auto vb0 = hn::LoadU(d, b + i);
        dot0 = hn::MulAdd(va0, vb0, dot0);
        norm_a0 = hn::MulAdd(va0, va0, norm_a0);
        norm_b0 = hn::MulAdd(vb0, vb0, norm_b0);

        const auto va1 = hn::LoadU(d, a + i + lanes);
        const auto vb1 = hn::LoadU(d, b + i + lanes);
        dot1 = hn::MulAdd(va1, vb1, dot1);
        norm_a1 = hn::MulAdd(va1, va1, norm_a1);
        norm_b1 = hn::MulAdd(vb1, vb1, norm_b1);

        const auto va2 = hn::LoadU(d, a + i + 2 * lanes);
        const auto vb2 = hn::LoadU(d, b + i + 2 * lanes);
        dot2 = hn::MulAdd(va2, vb2, dot2);
        norm_a2 = hn::MulAdd(va2, va2, norm_a2);
        norm_b2 = hn::MulAdd(vb2, vb2, norm_b2);

        const auto va3 = hn::LoadU(d, a + i + 3 * lanes);
        const auto vb3 = hn::LoadU(d, b + i + 3 * lanes);
        dot3 = hn::MulAdd(va3, vb3, dot3);
        norm_a3 = hn::MulAdd(va3, va3, norm_a3);
        norm_b3 = hn::MulAdd(vb3, vb3, norm_b3);
    }
    
    const size_t rounded_size_single = (size / lanes) * lanes;
    for (; i < rounded_size_single; i += lanes) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        dot0 = hn::MulAdd(va, vb, dot0);
        norm_a0 = hn::MulAdd(va, va, norm_a0);
        norm_b0 = hn::MulAdd(vb, vb, norm_b0);
    }
    
    auto dot_vec = hn::Add(hn::Add(dot0, dot1), hn::Add(dot2, dot3));
    auto norm_a_vec = hn::Add(hn::Add(norm_a0, norm_a1), hn::Add(norm_a2, norm_a3));
    auto norm_b_vec = hn::Add(hn::Add(norm_b0, norm_b1), hn::Add(norm_b2, norm_b3));

    dot = hn::GetLane(hn::SumOfLanes(d, dot_vec));
    norm_a = hn::GetLane(hn::SumOfLanes(d, norm_a_vec));
    norm_b = hn::GetLane(hn::SumOfLanes(d, norm_b_vec));
    
    for (; i < size; ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
}

// L1 (Manhattan) distance helper using SIMD
float L1DistanceSIMD(const float* a, const float* b, size_t size) {
    const hn::ScalableTag<float> d;
    const size_t lanes = hn::Lanes(d);
    auto sum0 = hn::Zero(d);
    auto sum1 = hn::Zero(d);
    auto sum2 = hn::Zero(d);
    auto sum3 = hn::Zero(d);
    
    size_t i = 0;
    const size_t unrolled_lanes = lanes * 4;
    const size_t rounded_size = (size / unrolled_lanes) * unrolled_lanes;
    for (; i < rounded_size; i += unrolled_lanes) {
        const auto va0 = hn::LoadU(d, a + i);
        const auto vb0 = hn::LoadU(d, b + i);
        const auto diff0 = hn::Sub(va0, vb0);
        sum0 = hn::Add(hn::Abs(diff0), sum0);

        const auto va1 = hn::LoadU(d, a + i + lanes);
        const auto vb1 = hn::LoadU(d, b + i + lanes);
        const auto diff1 = hn::Sub(va1, vb1);
        sum1 = hn::Add(hn::Abs(diff1), sum1);

        const auto va2 = hn::LoadU(d, a + i + 2 * lanes);
        const auto vb2 = hn::LoadU(d, b + i + 2 * lanes);
        const auto diff2 = hn::Sub(va2, vb2);
        sum2 = hn::Add(hn::Abs(diff2), sum2);

        const auto va3 = hn::LoadU(d, a + i + 3 * lanes);
        const auto vb3 = hn::LoadU(d, b + i + 3 * lanes);
        const auto diff3 = hn::Sub(va3, vb3);
        sum3 = hn::Add(hn::Abs(diff3), sum3);
    }
    
    const size_t rounded_size_single = (size / lanes) * lanes;
    for (; i < rounded_size_single; i += lanes) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        const auto diff = hn::Sub(va, vb);
        sum0 = hn::Add(hn::Abs(diff), sum0);
    }
    
    auto sum_vec = hn::Add(hn::Add(sum0, sum1), hn::Add(sum2, sum3));
    float sum = hn::GetLane(hn::SumOfLanes(d, sum_vec));
    for (; i < size; ++i) {
        sum += std::abs(a[i] - b[i]);
    }
    return sum;
}

} // namespace HWY_NAMESPACE
} // namespace fbvector
HWY_AFTER_NAMESPACE();

namespace fbvector {

std::optional<float> l2_distance(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) {
        return std::nullopt;
    }
    
    float squared_sum = HWY_NAMESPACE::L2DistanceSquaredSIMD(a.data(), b.data(), a.size());
    return std::sqrt(squared_sum);
}

std::optional<float> cosine_distance(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) {
        return std::nullopt;
    }
    
    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;
    
    HWY_NAMESPACE::CosinePartsSIMD(a.data(), b.data(), a.size(), dot, norm_a, norm_b);
    
    if (norm_a <= 0.0f || norm_b <= 0.0f) {
        return std::nullopt;
    }
    
    float similarity = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
    if (similarity > 1.0f) similarity = 1.0f;
    if (similarity < -1.0f) similarity = -1.0f;
    
    return 1.0f - similarity;
}

std::optional<float> dot_product(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) {
        return std::nullopt;
    }
    
    return HWY_NAMESPACE::DotProductSIMD(a.data(), b.data(), a.size());
}

std::optional<float> l1_distance(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) {
        return std::nullopt;
    }
    
    return HWY_NAMESPACE::L1DistanceSIMD(a.data(), b.data(), a.size());
}

std::optional<float> vector_norm(std::span<const float> a) {
    if (a.empty()) {
        return std::nullopt;
    }
    float dot = HWY_NAMESPACE::DotProductSIMD(a.data(), a.data(), a.size());
    return std::sqrt(dot);
}

} // namespace fbvector
