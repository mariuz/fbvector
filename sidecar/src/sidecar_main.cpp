#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "hnsw.h"

#define PORT 5005
#define MAGIC_SYNC 0xF1B10001
#define MAGIC_QUERY 0xF1B10002

void handle_client(int client_fd, fbvector::HNSWIndex& index) {
    while (true) {
        uint32_t magic = 0;
        ssize_t bytes = read(client_fd, &magic, sizeof(magic));
        if (bytes <= 0) break;

        if (magic == MAGIC_SYNC) {
            uint8_t action = 0;
            uint32_t id = 0;
            if (read(client_fd, &action, sizeof(action)) <= 0) break;
            if (read(client_fd, &id, sizeof(id)) <= 0) break;

            if (action == 1) { // Insert/Update
                uint32_t dim = 0;
                if (read(client_fd, &dim, sizeof(dim)) <= 0) break;
                std::vector<float> vec(dim);
                if (read(client_fd, vec.data(), dim * sizeof(float)) <= 0) break;
                
                index.insert(id, vec);
                std::cout << "[Sidecar] Inserted/Updated node " << id << " (dimension: " << dim << "), total size: " << index.size() << "\n";
            } else if (action == 2) { // Delete
                index.remove(id);
                std::cout << "[Sidecar] Deleted node " << id << ", total size: " << index.size() << "\n";
            }
        } else if (magic == MAGIC_QUERY) {
            uint32_t k = 0;
            uint32_t dim = 0;
            if (read(client_fd, &k, sizeof(k)) <= 0) break;
            if (read(client_fd, &dim, sizeof(dim)) <= 0) break;
            std::vector<float> query(dim);
            if (read(client_fd, query.data(), dim * sizeof(float)) <= 0) break;

            std::cout << "[Sidecar] KNN query: k=" << k << ", dim=" << dim << "\n";
            auto results = index.searchKnn(query, k);

            uint32_t res_count = results.size();
            write(client_fd, &res_count, sizeof(res_count));
            for (const auto& pair : results) {
                float dist = pair.first;
                uint32_t node_id = pair.second;
                write(client_fd, &dist, sizeof(dist));
                write(client_fd, &node_id, sizeof(node_id));
            }
        } else {
            std::cerr << "[Sidecar] Unknown magic: " << std::hex << magic << "\n";
            break;
        }
    }
    close(client_fd);
}

int main(int argc, char* argv[]) {
    int port = PORT;
    int dim = 1536;
    if (argc > 1) port = std::atoi(argv[1]);
    if (argc > 2) dim = std::atoi(argv[2]);

    std::cout << "=== Starting fbvector Sidecar Daemon ===\n";
    std::cout << "Listening on port: " << port << "\n";
    std::cout << "Default dimension: " << dim << "\n";

    fbvector::HNSWIndex index(dim);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to setsockopt SO_REUSEADDR\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Failed to listen\n";
        close(server_fd);
        return 1;
    }

    std::cout << "[Sidecar] Ready and accepting connections...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "Failed to accept connection\n";
            continue;
        }
        std::thread(handle_client, client_fd, std::ref(index)).detach();
    }

    close(server_fd);
    return 0;
}
