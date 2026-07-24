#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <random>
#include <mutex>
#include <optional>

namespace fbvector {

class HNSWIndex {
public:
    HNSWIndex(int dim, int M = 16, int efConstruction = 64, int efSearch = 32);

    void insert(int id, const std::vector<float>& vec);
    void remove(int id);
    std::vector<std::pair<float, int>> searchKnn(const std::vector<float>& query, int k) const;
    size_t size() const;

private:
    float distance(const std::vector<float>& a, const std::vector<float>& b) const;
    int getRandomLayer();

    struct Node {
        int id;
        std::vector<float> vec;
        std::vector<std::vector<int>> neighbors; // neighbors[layer]
    };

    int dim_;
    int M_;
    int maxM_;
    int maxM0_;
    int efConstruction_;
    int efSearch_;
    double mL_;
    int maxLayer_;
    int enterNode_; // entry point node ID

    std::unordered_map<int, Node> nodes_;
    mutable std::default_random_engine rng_;
    mutable std::mutex mutex_;
};

} // namespace fbvector
