#!/usr/bin/env python3
import subprocess
import tempfile
import os
import sys
import time

# Dynamic path resolution for portability between host and CI environments
ISQL_PATH = os.environ.get("ISQL_PATH")
if not ISQL_PATH:
    for path in ["/opt/firebird/bin/isql", "/usr/bin/isql-fb", "/usr/bin/isql"]:
        if os.path.exists(path):
            ISQL_PATH = path
            break
if not ISQL_PATH:
    ISQL_PATH = "isql-fb"

UDR_DIR = os.environ.get("UDR_DIR")
if not UDR_DIR:
    for path in ["/opt/firebird/plugins/udr", "/usr/lib/firebird/3.0/plugins/udr", "/usr/lib/x86_64-linux-gnu/firebird/3.0/plugins/udr", "/usr/lib/aarch64-linux-gnu/firebird/3.0/plugins/udr"]:
        if os.path.exists(path):
            UDR_DIR = path
            break
if not UDR_DIR:
    UDR_DIR = "/opt/firebird/plugins/udr"

SO_PATH = "build/lib/libfbvector.so"
SQL_INSTALL = "sql/install.sql"
DB_PASSWORD = os.environ.get("FIREBIRD_PASSWORD", "masterkey")

def run_cmd(cmd, input_data=None):
    res = subprocess.run(cmd, input=input_data, capture_output=True, text=True)
    return res.returncode, res.stdout, res.stderr

def main():
    print("=== Running Integration Tests for fbvector ===")

    # 1. Check prerequisites
    if not os.path.exists(SO_PATH):
        print(f"Error: Compiled UDR shared library not found at {SO_PATH}. Run build first.")
        sys.exit(1)
        
    if not os.path.exists(SQL_INSTALL):
        print(f"Error: Installation SQL script not found at {SQL_INSTALL}.")
        sys.exit(1)

    # 2. Copy the shared library to plugins/udr
    print("Copying libfbvector.so to Firebird plugins directory...")
    code, stdout, stderr = run_cmd(["sudo", "cp", SO_PATH, os.path.join(UDR_DIR, "libfbvector.so")])
    if code != 0:
        print(f"Error copying UDR plugin:\nStdout: {stdout}\nStderr: {stderr}")
        sys.exit(1)

    # 3. Create a temporary database
    db_fd, db_path = tempfile.mkstemp(suffix=".fdb", prefix="fbvector_test_")
    os.close(db_fd)
    # Remove it so isql can create a fresh one
    os.remove(db_path)

    print(f"Creating temporary Firebird database at {db_path}...")
    code, stdout, stderr = run_cmd([ISQL_PATH, "-user", "sysdba", "-password", DB_PASSWORD], 
                                   input_data=f"CREATE DATABASE '{db_path}';\n")
    if code != 0:
        print(f"Failed to create database:\nStdout: {stdout}\nStderr: {stderr}")
        sys.exit(1)

    # 3.5 Start the optional sidecar if built
    sidecar_proc = None
    sidecar_bin = "build/bin/fbvector_sidecar"
    if os.path.exists(sidecar_bin):
        print("Starting fbvector_sidecar background process...")
        try:
            sidecar_proc = subprocess.Popen([sidecar_bin, "5005", "3"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            # Give it a moment to start
            time.sleep(0.5)
        except Exception as e:
            print(f"Warning: Failed to launch sidecar: {e}")

    try:
        # 4. Register the functions
        print("Registering fbvector UDR functions...")
        code, stdout, stderr = run_cmd([ISQL_PATH, "-user", "sysdba", "-password", DB_PASSWORD, 
                                       "-input", SQL_INSTALL, db_path])
        if code != 0:
            print(f"Failed to register functions:\nStdout: {stdout}\nStderr: {stderr}")
            sys.exit(1)

        # 5. Define tests
        tests = [
            # (Test Name, SQL Query, Expected Substring in Output, Expect Success)
            (
                "Vector dimensions",
                "SELECT vector_dims(vector_from_text('[1.0, 2.0, 3.0, 4.0]')) FROM rdb$database;",
                "4",
                True
            ),
            (
                "Vector norm (magnitude)",
                "SELECT vector_norm(vector_from_text('[3.0, 4.0]')) FROM rdb$database;",
                "5.000000",
                True
            ),
            (
                "Vector from/to text roundtrip",
                "SELECT vector_to_text(vector_from_text('[1.2, -3.4, 5.67]')) FROM rdb$database;",
                "[1.200000,-3.400000,5.670000]",
                True
            ),
            (
                "L2 Distance calculation",
                "SELECT vector_l2_distance(vector_from_text('[1.0, 2.0]'), vector_from_text('[4.0, 6.0]')) FROM rdb$database;",
                "5.000000",
                True
            ),
            (
                "L1 (Manhattan) Distance calculation",
                "SELECT vector_l1_distance(vector_from_text('[1.0, 2.0, 3.0]'), vector_from_text('[4.0, -1.0, 6.0]')) FROM rdb$database;",
                "9.000000",
                True
            ),
            (
                "Inner Product calculation",
                "SELECT vector_inner_product(vector_from_text('[1.0, 2.0, 3.0]'), vector_from_text('[4.0, 5.0, 6.0]')) FROM rdb$database;",
                "32.000000",
                True
            ),
            (
                "Cosine Distance orthogonal",
                "SELECT vector_cosine_distance(vector_from_text('[1.0, 0.0]'), vector_from_text('[0.0, 1.0]')) FROM rdb$database;",
                "1.000000",
                True
            ),
            (
                "Cosine Distance identical",
                "SELECT vector_cosine_distance(vector_from_text('[1.5, 3.0]'), vector_from_text('[3.0, 6.0]')) FROM rdb$database;",
                "0.000000",
                True
            ),
            (
                "SQL_TEXT/VARCHAR fast overload test",
                "SELECT vector_l2_distance(CAST(vector_from_text('[1.0, 2.0]') AS VARCHAR(32) CHARACTER SET OCTETS), CAST(vector_from_text('[4.0, 6.0]') AS VARCHAR(32) CHARACTER SET OCTETS)) FROM rdb$database;",
                "5.000000",
                True
            ),
            (
                "Null argument handling",
                "SELECT vector_l2_distance(NULL, vector_from_text('[1.0, 2.0]')) FROM rdb$database;",
                "Vector parameter cannot be NULL",
                False
            ),
            (
                "Dimension mismatch handling",
                "SELECT vector_l2_distance(vector_from_text('[1.0, 2.0]'), vector_from_text('[1.0, 2.0, 3.0]')) FROM rdb$database;",
                "L2 distance failed: dimension mismatch",
                False
            ),
            (
                "Malformed text representation parsing",
                "SELECT vector_from_text('[1.0, abc]') FROM rdb$database;",
                "Failed to parse vector text representation",
                False
            ),
            (
                "Sidecar synchronization trigger test",
                "SELECT vector_sidecar_sync(1, 1, vector_from_text('[1.0, 2.0, 3.0]')) FROM rdb$database;",
                "0",
                True
            ) if os.path.exists(sidecar_bin) else None
        ]
        # Filter out disabled tests
        tests = [t for t in tests if t is not None]

        # 6. Execute tests
        failed = 0
        for name, query, expected, expect_success in tests:
            print(f"Running test: {name}... ", end="", flush=True)
            # We use SET LIST ON; to make output parsing easy and robust
            full_query = f"SET LIST ON;\n{query}\n"
            code, stdout, stderr = run_cmd([ISQL_PATH, "-user", "sysdba", "-password", DB_PASSWORD, db_path], 
                                           input_data=full_query)
            
            combined_output = stdout + stderr
            success = (code == 0) if expect_success else (code != 0)
            
            if success and (expected in combined_output):
                print("PASSED")
            else:
                print("FAILED")
                print(f"  Query: {query}")
                print(f"  Expected: {expected}")
                print(f"  Exit code: {code} (Expected success: {expect_success})")
                print(f"  Stdout: {stdout.strip()}")
                print(f"  Stderr: {stderr.strip()}")
                failed += 1

        if failed > 0:
            print(f"\nIntegration tests failed: {failed} failed test(s).")
            sys.exit(1)
        else:
            print("\nAll integration tests passed successfully!")

    finally:
        # Stop sidecar if running
        if sidecar_proc is not None:
            print("Stopping fbvector_sidecar process...")
            sidecar_proc.terminate()
            try:
                sidecar_proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                sidecar_proc.kill()

        # Cleanup
        if os.path.exists(db_path):
            print("Cleaning up temporary database...")
            try:
                os.remove(db_path)
            except Exception as e:
                print(f"Error removing {db_path}: {e}")

if __name__ == "__main__":
    main()
