#include "fbvector/binary_layout.h"
#include <cstring>

namespace fbvector {

bool validate_vector_data(std::span<const uint8_t> data) {
    if (data.size() < sizeof(VectorHeader)) {
        return false;
    }
    
    VectorHeader header;
    std::memcpy(&header, data.data(), sizeof(VectorHeader));
    
    size_t expected_size = sizeof(VectorHeader) + header.dimensions * sizeof(float);
    return data.size() == expected_size;
}

std::vector<uint8_t> serialize_vector(std::span<const float> vec) {
    if (vec.size() > 65535) {
        return {};
    }
    uint16_t dimensions = static_cast<uint16_t>(vec.size());
    VectorHeader header;
    header.dimensions = dimensions;
    header.flags = 0; // Default float32 format
    
    size_t size = sizeof(VectorHeader) + dimensions * sizeof(float);
    std::vector<uint8_t> buffer(size);
    std::memcpy(buffer.data(), &header, sizeof(VectorHeader));
    
    if (dimensions > 0) {
        std::memcpy(buffer.data() + sizeof(VectorHeader), vec.data(), dimensions * sizeof(float));
    }
    
    return buffer;
}

std::optional<std::span<const float>> deserialize_vector(std::span<const uint8_t> data) {
    if (!validate_vector_data(data)) {
        return std::nullopt;
    }
    
    VectorHeader header;
    std::memcpy(&header, data.data(), sizeof(VectorHeader));
    
    if (header.flags != 0) {
        return std::nullopt; // Only support float32 (flag=0) for now
    }
    
    const float* float_data = reinterpret_cast<const float*>(data.data() + sizeof(VectorHeader));
    return std::span<const float>(float_data, header.dimensions);
}

} // namespace fbvector
