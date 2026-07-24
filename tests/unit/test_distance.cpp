#include <gtest/gtest.h>
#include "fbvector/distance.h"
#include "fbvector/binary_layout.h"
#include <vector>
#include <cmath>

TEST(DistanceTest, L2DistanceBasic) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f};

    auto dist = fbvector::l2_distance(a, b);
    ASSERT_TRUE(dist.has_value());
    // L2 = sqrt((1-4)^2 + (2-5)^2 + (3-6)^2) = sqrt(9 + 9 + 9) = sqrt(27) ≈ 5.19615
    EXPECT_NEAR(dist.value(), std::sqrt(27.0f), 1e-5f);
}

TEST(DistanceTest, L2DistanceDimensionMismatch) {
    std::vector<float> a = {1.0f, 2.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};

    auto dist = fbvector::l2_distance(a, b);
    EXPECT_FALSE(dist.has_value());
}

TEST(DistanceTest, CosineDistanceBasic) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};

    auto dist = fbvector::cosine_distance(a, b);
    ASSERT_TRUE(dist.has_value());
    // Cosine similarity = 0.0, Cosine distance = 1.0 - 0.0 = 1.0
    EXPECT_NEAR(dist.value(), 1.0f, 1e-5f);

    std::vector<float> c = {2.0f, 0.0f};
    auto dist2 = fbvector::cosine_distance(a, c);
    ASSERT_TRUE(dist2.has_value());
    // Cosine similarity = 1.0, Cosine distance = 1.0 - 1.0 = 0.0
    EXPECT_NEAR(dist2.value(), 0.0f, 1e-5f);
}

TEST(DistanceTest, CosineDistanceZeroVector) {
    std::vector<float> a = {0.0f, 0.0f};
    std::vector<float> b = {1.0f, 2.0f};

    auto dist = fbvector::cosine_distance(a, b);
    EXPECT_FALSE(dist.has_value());
}

TEST(DistanceTest, DotProductBasic) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f};

    auto dist = fbvector::dot_product(a, b);
    ASSERT_TRUE(dist.has_value());
    // Dot Product = 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_NEAR(dist.value(), 32.0f, 1e-5f);
}

TEST(DistanceTest, L1DistanceBasic) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, -1.0f, 6.0f};

    auto dist = fbvector::l1_distance(a, b);
    ASSERT_TRUE(dist.has_value());
    // L1 = |1-4| + |2-(-1)| + |3-6| = 3 + 3 + 3 = 9
    EXPECT_NEAR(dist.value(), 9.0f, 1e-5f);
}

TEST(DistanceTest, L1DistanceDimensionMismatch) {
    std::vector<float> a = {1.0f, 2.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f};

    auto dist = fbvector::l1_distance(a, b);
    EXPECT_FALSE(dist.has_value());
}

TEST(DistanceTest, VectorNormBasic) {
    std::vector<float> a = {3.0f, 4.0f};

    auto norm = fbvector::vector_norm(a);
    ASSERT_TRUE(norm.has_value());
    // Norm = sqrt(3^2 + 4^2) = 5
    EXPECT_NEAR(norm.value(), 5.0f, 1e-5f);
}

TEST(DistanceTest, VectorNormEmpty) {
    std::vector<float> a = {};

    auto norm = fbvector::vector_norm(a);
    EXPECT_FALSE(norm.has_value());
}

TEST(BinaryLayoutTest, SerializationDeserialization) {
    std::vector<float> vec = {1.5f, -2.0f, 3.75f, 0.0f};
    
    // Serialize
    std::vector<uint8_t> serialized = fbvector::serialize_vector(vec);
    
    // Expect header (4 bytes for dimensions) + 4 floats * 4 bytes = 20 bytes
    EXPECT_EQ(serialized.size(), 20);
    
    // Validate
    EXPECT_TRUE(fbvector::validate_vector_data(serialized));
    
    // Deserialize
    auto deserialized = fbvector::deserialize_vector(serialized);
    ASSERT_TRUE(deserialized.has_value());
    
    EXPECT_EQ(deserialized->size(), vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        EXPECT_EQ((*deserialized)[i], vec[i]);
    }
}

TEST(BinaryLayoutTest, InvalidData) {
    std::vector<uint8_t> invalid_short = {1, 0, 0}; // too short for header
    EXPECT_FALSE(fbvector::validate_vector_data(invalid_short));
    EXPECT_FALSE(fbvector::deserialize_vector(invalid_short).has_value());
    
    // Header indicates 2 floats (8 bytes), but we only give 4 bytes of data (total 8 bytes)
    std::vector<uint8_t> invalid_len = {2, 0, 0, 0, 1, 2, 3, 4};
    EXPECT_FALSE(fbvector::validate_vector_data(invalid_len));
    EXPECT_FALSE(fbvector::deserialize_vector(invalid_len).has_value());

    // Header indicates 2 floats (8 bytes) with flag = 1 (unsupported), total size = 12 bytes
    std::vector<uint8_t> invalid_flag = {2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(fbvector::validate_vector_data(invalid_flag));
    EXPECT_FALSE(fbvector::deserialize_vector(invalid_flag).has_value());
}
