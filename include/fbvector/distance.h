#pragma once
#include <span>
#include <optional>

namespace fbvector {

// Calculates the L2 (Euclidean) distance between two vectors of the same dimension.
// Returns std::nullopt if dimensions do not match.
std::optional<float> l2_distance(std::span<const float> a, std::span<const float> b);

// Calculates the cosine distance between two vectors of the same dimension.
// Cosine distance is defined as 1 - cosine_similarity.
// Returns std::nullopt if dimensions do not match, or if either vector has a zero magnitude.
std::optional<float> cosine_distance(std::span<const float> a, std::span<const float> b);

// Calculates the dot product (inner product) between two vectors of the same dimension.
// Returns std::nullopt if dimensions do not match.
std::optional<float> dot_product(std::span<const float> a, std::span<const float> b);

// Calculates the L1 (Manhattan) distance between two vectors of the same dimension.
// Returns std::nullopt if dimensions do not match.
std::optional<float> l1_distance(std::span<const float> a, std::span<const float> b);

// Calculates the L2 norm (magnitude) of a vector.
// Returns std::nullopt if the vector is empty.
std::optional<float> vector_norm(std::span<const float> a);

} // namespace fbvector
