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

Alternatively, to run MTR directly from the VillageSQL build directory, pass the paths of the main suite and all sub-suites (such as `spider` and `schemas_0` through `schemas_6`):
```bash
cd /home/maxdemarzi/build/villagesql/mysql-test
perl mysql-test-run.pl --suite=/home/maxdemarzi/vsql-schematron/mysql-test,/home/maxdemarzi/vsql-schematron/mysql-test/spider,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_0,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_1,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_2,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_3,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_4,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_5,/home/maxdemarzi/vsql-schematron/mysql-test/schemas_6
```

## Regenerating Test Results

To record/regenerate test results for a specific test case, specify the suite and test name with `--record`:
```bash
perl mysql-test-run.pl --suite=/home/maxdemarzi/vsql-schematron/mysql-test/schemas_0 <test_name> --record
```

## Test Suites & Folders

| Suite Directory | Description |
|-----------------|-------------|
| `mysql-test` | Contains basic integration tests (e.g. `schematron_basic.test` for basic install/uninstall and basic function tests) |
| `mysql-test/spider` | Contains spider-related test cases |
| `mysql-test/schemas_0` to `schemas_6` | Contains schema relationship test cases, partitioned into 7 directories to keep individual directories small |
