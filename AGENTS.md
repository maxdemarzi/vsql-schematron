# AGENTS.md

This file provides guidance to AI coding assistants when working with code in this repository.

## Project Overview

This is the `vsql-schematron` extension for VillageSQL, providing rule-based validation for XML documents.

## Build System

- **Build**: `./build.sh` (produces `build/vsql_schematron.veb`)
- **Install**: `cd build && make install`
- **Install in MySQL**: `INSTALL EXTENSION vsql_schematron;`

Set `VillageSQL_BUILD_DIR` to point to the local VillageSQL build directory.

## Architecture

- `src/vsql_schematron.cc` - VEF implementation
- `manifest.json` - Extension metadata
- `CMakeLists.txt` - Build script
- `mysql-test/` - MTR tests
