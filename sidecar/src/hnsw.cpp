#include "hnsw.h"
#include <cmath>
#include <algorithm>

namespace fbvector {

HNSWIndex::HNSWIndex(int dim, int M, int efConstruction, int efSearch)
    : dim_(dim), M_(M), maxM_(M), maxM0_(M * 2),
      efConstruction_(efConstruction), efSearch_(efSearch),
      mL_(1.0 / std::log(M)), maxLayer_(-1), enterNode_(-1), rng_(42) {}

float HNSWIndex::distance(const std::vector<float>& a, const std::vector<float>& b) const {
    float diff_sum = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        float diff = a[i] - b[i];
        diff_sum += diff * diff;
    }
    return std::sqrt(diff_sum);
}

int HNSWIndex::getRandomLayer() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double r = -std::log(dist(rng_)) * mL_;
    return static_cast<int>(r);
}

size_t HNSWIndex::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

void HNSWIndex::insert(int id, const std::vector<float>& vec) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Update if already exists
    if (nodes_.find(id) != nodes_.end()) {
        // Simple removal first
        lock.unlock();
        remove(id);
        lock.lock();
    }

    int insertLayer = getRandomLayer();

    Node newNode;
    newNode.id = id;
    newNode.vec = vec;
    newNode.neighbors.resize(insertLayer + 1);

    if (nodes_.empty()) {
        nodes_[id] = std::move(newNode);
        enterNode_ = id;
        maxLayer_ = insertLayer;
        return;
    }

    int curr = enterNode_;
    float curr_dist = distance(vec, nodes_.at(curr).vec);

    // 1. Search from maxLayer down to insertLayer + 1 (greedy search)
    for (int l = maxLayer_; l > insertLayer; --l) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int neighbor : nodes_.at(curr).neighbors[l]) {
                float d = distance(vec, nodes_.at(neighbor).vec);
                if (d < curr_dist) {
                    curr_dist = d;
                    curr = neighbor;
                    changed = true;
                }
            }
        }
    }

    // 2. Insert from insertLayer down to 0
    int ef = efConstruction_;
    for (int l = std::min(maxLayer_, insertLayer); l >= 0; --l) {
        std::priority_queue<std::pair<float, int>> v; // max-heap (furthest elements at top)
        std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> C; // min-heap (closest at top)
        std::unordered_set<int> visited;

        v.push({curr_dist, curr});
        C.push({curr_dist, curr});
        visited.insert(curr);

        while (!C.empty()) {
            auto curr_cand = C.top();
            C.pop();
            auto furthest_result = v.top();
            if (curr_cand.first > furthest_result.first) {
                break;
            }

            for (int neighbor : nodes_.at(curr_cand.second).neighbors[l]) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    float nd = distance(vec, nodes_.at(neighbor).vec);
                    if (nd < furthest_result.first || v.size() < static_cast<size_t>(ef)) {
                        C.push({nd, neighbor});
                        v.push({nd, neighbor});
                        if (v.size() > static_cast<size_t>(ef)) {
                            v.pop();
                        }
                    }
                }
            }
        }

        // Connect newNode to top candidates
        std::vector<int> candidates;
        while (!v.empty()) {
            candidates.push_back(v.top().second);
            v.pop();
        }

        int limit = (l == 0) ? maxM0_ : maxM_;
        if (candidates.size() > static_cast<size_t>(limit)) {
            candidates.resize(limit);
        }

        for (int cand : candidates) {
            newNode.neighbors[l].push_back(cand);
            nodes_.at(cand).neighbors[l].push_back(id);
            if (nodes_.at(cand).neighbors[l].size() > static_cast<size_t>(limit)) {
                nodes_.at(cand).neighbors[l].resize(limit);
            }
        }

        if (!candidates.empty()) {
            curr = candidates.front();
            curr_dist = distance(vec, nodes_.at(curr).vec);
        }
    }

    nodes_[id] = std::move(newNode);

    if (insertLayer > maxLayer_) {
        maxLayer_ = insertLayer;
        enterNode_ = id;
    }
}

void HNSWIndex::remove(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (nodes_.find(id) == nodes_.end()) return;

    const auto& node = nodes_.at(id);
    for (int l = 0; l < static_cast<int>(node.neighbors.size()); ++l) {
        for (int neighbor : node.neighbors[l]) {
            if (nodes_.find(neighbor) != nodes_.end()) {
                auto& list = nodes_.at(neighbor).neighbors[l];
                list.erase(std::remove(list.begin(), list.end(), id), list.end());
            }
        }
    }

    nodes_.erase(id);

    if (enterNode_ == id) {
        if (!nodes_.empty()) {
            enterNode_ = nodes_.begin()->first;
            maxLayer_ = static_cast<int>(nodes_.begin()->second.neighbors.size() - 1);
        } else {
            enterNode_ = -1;
            maxLayer_ = -1;
        }
    }
}

std::vector<std::pair<float, int>> HNSWIndex::searchKnn(const std::vector<float>& query, int k) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (nodes_.empty()) return {};

    int curr = enterNode_;
    float curr_dist = distance(query, nodes_.at(curr).vec);

    // Greedy search down to layer 1
    for (int l = maxLayer_; l > 0; --l) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int neighbor : nodes_.at(curr).neighbors[l]) {
                float d = distance(query, nodes_.at(neighbor).vec);
                if (d < curr_dist) {
                    curr_dist = d;
                    curr = neighbor;
                    changed = true;
                }
            }
        }
    }

    // Candidate search at layer 0 using efSearch
    std::priority_queue<std::pair<float, int>> v; // max-heap
    std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> C; // min-heap
    std::unordered_set<int> visited;

    v.push({curr_dist, curr});
    C.push({curr_dist, curr});
    visited.insert(curr);

    while (!C.empty()) {
        auto curr_cand = C.top();
        C.pop();
        auto furthest_result = v.top();
        if (curr_cand.first > furthest_result.first) {
            break;
        }

        for (int neighbor : nodes_.at(curr_cand.second).neighbors[0]) {
            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                float nd = distance(query, nodes_.at(neighbor).vec);
                if (nd < furthest_result.first || v.size() < static_cast<size_t>(efSearch_)) {
                    C.push({nd, neighbor});
                    v.push({nd, neighbor});
                    if (v.size() > static_cast<size_t>(efSearch_)) {
                        v.pop();
                    }
                }
            }
        }
    }

    std::vector<std::pair<float, int>> result;
    while (!v.empty()) {
        result.push_back(v.top());
        v.pop();
    }
    std::reverse(result.begin(), result.end());
    if (result.size() > static_cast<size_t>(k)) {
        result.resize(k);
    }
    return result;
}

} // namespace fbvector
