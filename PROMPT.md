# Firebird Vector Extension (`fbvector`) - Project Plan & Execution Strategy

## Executive Summary
The goal of this project is to build an open-source C++ User-Defined Routine (UDR) library for the Firebird Database Engine (Firebird 3.0+ / 4.0+ / 5.0+). The plugin brings high-dimensional vector storage, SIMD-accelerated distance metrics, and vector utilities to Firebird, inspired by PostgreSQL's `pgvector`.

---

## Technical Stack & Dependencies
* **Language:** C++20 standard
* **Target Database:** Firebird (UDR API v3/v4)
* **Build System:** CMake (3.20+)
* **SIMD Optimizations:** Highway (Google) or standard compiler flags (`-mavx2`, `-mfma`, etc.)
* **Testing Framework:** GoogleTest (GTest) for unit tests + Python (`fdb` / `firebird-driver`) for integration tests
* **CI/CD:** GitHub Actions (Linux gcc/clang, Windows MSVC)

---

## Execution Plan & Task Breakdown

### Phase 1: Project Scaffolding & Build System
- [x] Create basic repository structure:
  ```text
  ├── CMakeLists.txt
  ├── README.md
  ├── LICENSE (MIT or BSL)
  ├── docs/
  │   └── architecture.md
  ├── include/
  │   └── fbvector/
  │       ├── distance.h
  │       ├── binary_layout.h
  │       └── udr_entry.h
  ├── src/
  │   ├── distance.cpp
  │   ├── binary_layout.cpp
  │   └── udr_entry.cpp
  ├── sql/
  │   └── install.sql
  └── tests/
      ├── unit/
      └── integration/
