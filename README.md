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

## Known Limitations

- Aggregate functions: Not supported.
- Extension upgrade path: Upgrade requires manual reinstall via `UNINSTALL` and `INSTALL`.

## Testing

For testing, refer to [TESTING.md](file:///home/maxdemarzi/vsql-schematron/TESTING.md).

## Reporting Bugs and Requesting Features

Please open an issue on the repository page.

## Contact

Join our community on Discord: `https://discord.gg/KSr6whd3Fr`

## License

This project is licensed under the GPL-2.0 license.
