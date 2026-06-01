# Testing vsql-schematron

This document describes how to run the tests for the `vsql-schematron` extension.

Testing is done using the **MySQL Test Runner (MTR)** framework. MTR manages temporary database servers automatically to run SQL scripts (located in `mysql-test/t/*.test`) and validates their correctness by comparing their stdout output with pre-defined results (located in `mysql-test/r/*.result`).

## Environment Variables

- `VillageSQL_BUILD_DIR`: The path to the VillageSQL build directory.

## Build and Install

Before running tests, the extension must be built:
```bash
./build.sh
```

Then install the extension to the server's VEB directory:
```bash
cd build && make install
```

## Running Tests

The recommended way to build and run the test suite is using the local CI script from the extension root:
```bash
export VILLAGESQL_BUILD_DIR=/home/maxdemarzi/build/villagesql
./local-ci.sh
```

Alternatively, to run MTR directly from the VillageSQL build directory:
```bash
cd /home/maxdemarzi/build/villagesql/mysql-test
perl mysql-test-run.pl --suite=/home/maxdemarzi/vsql-schematron/mysql-test
```

## Regenerating Test Results

To record/regenerate test results:
```bash
perl mysql-test-run.pl --suite=/home/maxdemarzi/vsql-schematron/mysql-test --record
```

## Test Files

| Test File | Description |
|-----------|-------------|
| `schematron_basic.test` | Tests the hello_world() function and basic install/uninstall flow |
