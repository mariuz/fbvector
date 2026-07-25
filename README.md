# fbvector: Firebird Vector Extension (v1.0)

`fbvector` is an open-source, high-performance C++ User-Defined Routine (UDR) library for the Firebird Database Engine (Firebird 3.0, 4.0, and 5.0). It brings vector storage and similarity searches to Firebird, inspired by PostgreSQL's `pgvector`.

## Features
- **SIMD Optimized Math**: Core vector calculations are accelerated using Google Highway, with 4-way loop unrolling to maximize CPU instruction-level parallelism.
- **Multiple Distance Metrics**:
  - `vector_l2_distance` (Euclidean Distance): $\sqrt{\sum (a_i - b_i)^2}$
  - `vector_cosine_distance` (Cosine Distance): $1 - \frac{\mathbf{a} \cdot \mathbf{b}}{\|\mathbf{a}\| \|\mathbf{b}\|}$
  - `vector_inner_product` (Dot Product): $\mathbf{a} \cdot \mathbf{b}$
  - `vector_l1_distance` (Manhattan Distance): $\sum |a_i - b_i|$
- **Vector Utilities**:
  - `vector_norm` (Vector L2 magnitude): $\sqrt{\sum a_i^2}$
  - `vector_dims` (Extracts vector dimension count)
  - `vector_from_text` (Parses a JSON-style string like `'[1.0, 2.0, 3.0]'` into packed floats)
  - `vector_to_text` (Converts a packed binary vector back into a readable string)
- **Compact Packed Binary Format**: Uses a packed 4-byte header (`uint16_t dimensions`, `uint16_t flags`) followed by contiguous 32-bit IEEE 754 floating-point values.
- **Flexible SQL Argument Binding**: Supports inline VARCHAR columns (`VARCHAR(N) CHARACTER SET OCTETS`) as well as out-of-line `BLOB SUB_TYPE BINARY` objects.

## Repository Structure
```text
├── .github/
│   └── workflows/
│       └── ci.yml              # GitHub Actions build matrix (GCC, Clang, MSVC)
├── CMakeLists.txt              # Root build configuration
├── README.md                   # Project overview and usage guide
├── LICENSE                     # MIT License
├── docs/
│   └── architecture.md         # Design decisions and binary format specification
├── include/
│   └── fbvector/
│       ├── distance.h          # Distance metrics declarations
│       ├── binary_layout.h     # Vector serialization/validation declarations
│       └── udr_entry.h         # Firebird UDR plugin helper declarations
├── src/
│   ├── distance.cpp            # Distance math implementation
│   ├── binary_layout.cpp       # Serialization helper implementation
│   └── udr_entry.cpp           # Firebird UDR entry points and utilities
├── sql/
│   ├── install.sql             # SQL script to register the functions in Firebird
│   └── examples.sql            # Practical SQL examples (table schemas, k-NN, indexes)
└── tests/
    ├── unit/
    │   └── test_distance.cpp   # GoogleTest suite for core calculations
    └── integration/
        └── test_integration.py # Integration testing suite for live Firebird instance
```

## Prerequisites
- **C++ Compiler**: GCC (11+) or Clang (14+) on Linux; MSVC (2022) on Windows.
- **Firebird Development Headers**: `firebird-dev` package (includes `ibase.h`, `UdrCppEngine.h`, etc.). Typically installed at `/opt/firebird` or standard system paths.
- **Build Tools**: CMake (3.20+) and Make/Ninja.

## Building the Project

Configure and build the shared library using CMake:

```bash
# Configure the project
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile the library and tests
cmake --build build -j$(nproc)
```

The resulting UDR shared library will be created at `build/lib/libfbvector.so` (or `build/lib/fbvector.dll` on Windows).

## Running Tests

### Unit Tests
Run the unit test suite using `ctest`:

```bash
ctest --test-dir build --output-on-failure
```

### Integration Tests
Run the integration test suite, which launches a temporary Firebird instance, registers all UDR functions, and performs end-to-end calculations:

```bash
python3 tests/integration/test_integration.py
```

## Installing the Extension

### Option A: Using Pre-built Release Binaries

1. **Download Release Package**: Visit [GitHub Releases](https://github.com/mariuz/fbvector/releases) and download the compiled archive matching your system:
   * **Linux**: `fbvector-v1.0.0-linux-x64.tar.gz` (includes `libfbvector.so`, `fbvector_sidecar` daemon, and `install.sql`)
   * **Windows**: `fbvector-v1.0.0-windows-x64.zip` (includes `fbvector.dll`, `fbvector_sidecar.exe` daemon, and `install.sql`)
2. **Extract & Copy to Plugins**: Copy the library file to your Firebird installation's UDR plugins directory:
   * **Linux**: `sudo cp libfbvector.so /opt/firebird/plugins/udr/`
   * **Windows**: Copy `fbvector.dll` to `C:\Program Files\Firebird\Firebird_5_0\plugins\udr\` (or your Firebird 6 path).
3. **Register UDR Functions**: Execute the registration script inside your database:
   ```bash
   /opt/firebird/bin/isql -user sysdba -password <your_password> -input install.sql your_database.fdb
   ```

### Option B: Building from Source

1. **Deploy Compiled UDR Library**: Copy the compiled shared library from your `build/` directory:
   * **Linux**: `sudo cp build/lib/libfbvector.so /opt/firebird/plugins/udr/`
   * **Windows**: Copy `fbvector.dll` to `C:\Program Files\Firebird\Firebird_5_0\plugins\udr\` (or your Firebird 6 path).

2. **Register UDR Functions**: Register the external functions in your database by running the installation SQL script:
   ```bash
   isql-fb -user sysdba -password masterkey -input sql/install.sql your_database.fdb
   ```

## Quick SQL Usage Example

Below is a quick reference showing how to store embeddings and find nearest neighbors using Cosine distance:

```sql
-- 1. Create a table
CREATE TABLE products (
    id INT GENERATED BY DEFAULT AS IDENTITY PRIMARY KEY,
    name VARCHAR(100),
    embedding BLOB SUB_TYPE BINARY
);

-- 2. Insert vector embeddings
INSERT INTO products (name, embedding) VALUES (
    'Tablet', 
    vector_from_text('[0.15, -0.47, 0.79, 0.22]')
);

-- 3. Query exact nearest neighbors (k-NN) using Cosine Distance
SELECT id, name, vector_cosine_distance(embedding, vector_from_text('[0.10, -0.40, 0.80, 0.20]')) AS dist
FROM products
ORDER BY dist ASC;
```

For more advanced query examples, python integration tutorials, and expression index optimization, see [docs/examples.md](file:///home/ubuntu/work/fbvector/docs/examples.md) and [sql/examples.sql](file:///home/ubuntu/work/fbvector/sql/examples.sql).

## Future Roadmap (Post-v1.0 Considerations)
* **Scalar Quantization (SQ8)**: Compress float32 vectors to int8 to reduce BLOB storage footprints by 4x.
* **Batch Distance Functions**: UDR aggregate functions processing batches of vectors in single calls.
* **Optional External ANN Index Service / Sidecar**: Background daemon or shared memory cache maintaining HNSW graphs synced via DB triggers.
