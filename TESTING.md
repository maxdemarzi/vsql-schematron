# Testing vsql-schematron

This document describes how to run the tests for the `vsql-schematron` extension.

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

From the VillageSQL/MySQL build directory:
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
