#pragma once
#include <span>
#include <vector>
#include <cstdint>
#include <optional>

namespace fbvector {

// The binary header stored at the beginning of the serialized vector representation.
struct VectorHeader {
    uint16_t dimensions;
    uint16_t flags; // Flags representing representation (e.g., float32 vs float16)
};

// Validates whether the binary data represents a valid serialized vector.
bool validate_vector_data(std::span<const uint8_t> data);

// Serializes a float span into a binary byte vector.
std::vector<uint8_t> serialize_vector(std::span<const float> vec);

// Deserializes a binary byte span into a float span.
// Returns std::nullopt if the binary layout is invalid or truncated.
std::optional<std::span<const float>> deserialize_vector(std::span<const uint8_t> data);

} // namespace fbvector
