-- Firebird Vector Extension (fbvector) Installation SQL Script
-- Run this script in your target Firebird database to register the vector UDR functions.

-- 1. L2 (Euclidean) Distance
CREATE FUNCTION vector_l2_distance (
    v1 BLOB SUB_TYPE BINARY,
    v2 BLOB SUB_TYPE BINARY
) RETURNS DOUBLE PRECISION
    EXTERNAL NAME 'fbvector!vector_l2_distance'
    ENGINE UDR;

-- 2. Cosine Distance
CREATE FUNCTION vector_cosine_distance (
    v1 BLOB SUB_TYPE BINARY,
    v2 BLOB SUB_TYPE BINARY
) RETURNS DOUBLE PRECISION
    EXTERNAL NAME 'fbvector!vector_cosine_distance'
    ENGINE UDR;

-- 3. Inner Product / Dot Product
CREATE FUNCTION vector_inner_product (
    v1 BLOB SUB_TYPE BINARY,
    v2 BLOB SUB_TYPE BINARY
) RETURNS DOUBLE PRECISION
    EXTERNAL NAME 'fbvector!vector_inner_product'
    ENGINE UDR;

-- 4. L1 (Manhattan) Distance
CREATE FUNCTION vector_l1_distance (
    v1 BLOB SUB_TYPE BINARY,
    v2 BLOB SUB_TYPE BINARY
) RETURNS DOUBLE PRECISION
    EXTERNAL NAME 'fbvector!vector_l1_distance'
    ENGINE UDR;

-- 5. Vector Dimensions
CREATE FUNCTION vector_dims (
    v BLOB SUB_TYPE BINARY
) RETURNS INTEGER
    EXTERNAL NAME 'fbvector!vector_dims'
    ENGINE UDR;

-- 6. Vector Norm (Computes magnitude)
CREATE FUNCTION vector_norm (
    v BLOB SUB_TYPE BINARY
) RETURNS DOUBLE PRECISION
    EXTERNAL NAME 'fbvector!vector_norm'
    ENGINE UDR;

-- 7. Vector from Text (e.g., '[1.0, 2.0, 3.0]')
CREATE FUNCTION vector_from_text (
    t VARCHAR(8191) CHARACTER SET UTF8
) RETURNS BLOB SUB_TYPE BINARY
    EXTERNAL NAME 'fbvector!vector_from_text'
    ENGINE UDR;

-- 8. Vector to Text (returns readable string representation)
CREATE FUNCTION vector_to_text (
    v BLOB SUB_TYPE BINARY
) RETURNS VARCHAR(8191) CHARACTER SET UTF8
    EXTERNAL NAME 'fbvector!vector_to_text'
    ENGINE UDR;

-- 9. Vector Sidecar Sync (optional sidecar trigger notifier)
CREATE FUNCTION vector_sidecar_sync (
    id INTEGER,
    action INTEGER,
    v BLOB SUB_TYPE BINARY
) RETURNS INTEGER
    EXTERNAL NAME 'fbvector!vector_sidecar_sync'
    ENGINE UDR;
