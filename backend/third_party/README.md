# Third-Party Dependencies

This directory documents external dependencies that the backend integrates via CMake. The actual code is downloaded at configure-time using `FetchContent` so that we do not need to vendor the full sources inside the repository.

* **cpp-httplib** – header-only HTTP server/client used for REST endpoints. Pulled from https://github.com/yhirose/cpp-httplib (tag `v0.15.3`).
* **SQLite3** – provided by the host toolchain and linked via `find_package(SQLite3 REQUIRED)`.
