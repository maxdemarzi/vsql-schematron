# VillageSQL Schematron Extension

VillageSQL extension providing rule-based XML validation using Schematron patterns.

## Building

To build the extension, you need:
- VillageSQL build directory
- CMake 3.16 or higher
- C++ compiler with C++17 support

1. Run `./build.sh` or configure with CMake manually:
   ```bash
   mkdir build
   cd build
   cmake .. -DVillageSQL_BUILD_DIR=/home/maxdemarzi/build/villagesql
   make
   ```

## Installing

Install the extension by loading it in VillageSQL:
```sql
INSTALL EXTENSION vsql_schematron;
```

## Function Reference

### `hello_world()`
- **Returns:** `STRING`
- **Arguments:** None
- **Description:** Example hello-world function.

### `vsql_schema_cache_ready(db_name)`
- **Returns:** `INT`
- **Arguments:** `db_name` (STRING)
- **Description:** Returns `1` if the database schema cache is loaded and ready, `0` otherwise.

## Known Limitations

- Aggregate functions: Not supported.
- Extension upgrade path: Upgrade requires manual reinstall via `UNINSTALL` and `INSTALL`.
- Preview APIs: Uses `sql_query` and `thread_worker` preview capabilities, which may change between server builds.

## Testing

This project uses **MTR (MySQL Test Runner)**, the standard integration and regression testing framework for VillageSQL and MySQL. MTR automates starting and stopping temporary database server instances, executing SQL test scripts (`.test` files), and comparing the live database outputs against pre-recorded expected results (`.result` files).

The test suite includes basic integration tests, spider-specific tests, and thousands of schema validation tests organized across several subdirectories (`schemas_0` through `schemas_6`) to keep the repository structure clean and maintainable.

Build and run the test suite using the local CI script:
```bash
export VILLAGESQL_BUILD_DIR=/home/maxdemarzi/build/villagesql
./local-ci.sh
```

For more details, see [TESTING.md](file:///home/maxdemarzi/vsql-schematron/TESTING.md).

## Reporting Bugs and Requesting Features

Please open an issue on the repository page.

## Contact

Join our community on Discord: `https://discord.gg/KSr6whd3Fr`

## License

This project is licensed under the GPL-2.0 license.
