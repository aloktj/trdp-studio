TRDP Simulator Application

C++ Backend + Web Frontend | Multi-User | XML-Driven TRDP Stack | PD & MD Support

This repository contains the source code for a network-enabled TRDP application that provides:

A C/C++ backend (CMake build)

A browser-based web frontend (React or vanilla JS)

A TRDP communication engine based on a pre-built TRDP static library

User login system with admin/dev roles

XML-based TRDP configuration management

PD & MD monitoring + control

Local SQLite database for storing per-user configs & logs

Ability for multiple machines to run the same app and communicate with each other via TRDP.


This README is intentionally detailed so that ChatGPT Codex can generate files and modules consistently.


---

📌 Project Overview

This application runs on a single host machine (multiple hosts in same network).
Other machines in the same LAN can access the web UI through the browser (e.g., http://<host-ip>:8080).

If the same binary is run on several machines, each instance should communicate with the others using TRDP.
The only differences per machine:

The machine’s IP address / NIC config

The TRDP XML configuration selected by the user


Everything else is the same executable.


---

🧱 Overall Architecture

Backend – C/C++

CMake project producing a single executable: trdp_app

Uses:

cpp-httplib (embedded HTTP REST server)

sqlite3 (database)

TRDP stack prebuilt library

bcrypt or Argon2 for password hashing


REST API endpoints for:

Auth / user management

TRDP XML config management

Network configuration

PD communication

MD communication

Logs


Stores all persistent data in SQLite DB.


Frontend – Web App

Either:

React + TypeScript (recommended)
OR

Simple HTML + JavaScript (if needed)


Pages:

1. Login


2. TRDP Stack Configuration


3. Network Configuration


4. TRDP Communication Control (PD + MD)


5. Account Control


6. Help




---

🧩 Core Modules

1. Authentication & Users

Default accounts created on first launch:

admin / admin

dev / dev


Passwords are stored only as hashes (bcrypt/argon2)

Users can register new accounts

Per-user:

TRDP XML configurations

TRDP logs

Application logs



2. TRDP XML Configuration System

Users can:

Upload XML files

Validate them using TRDP validation API

Store & name configurations

View validation status (PASS/FAIL)

Click to preview XML content on UI

Activate exactly one configuration for the running instance


Only one config active at a time per machine instance.

3. Network Configuration

Select network interface (e.g., enp0s8)

Set:

Local IP address

Multicast groups

TRDP communication parameters


Apply configuration → restarts TRDP engine.


4. TRDP Communication Control

PD (Process Data)

Outgoing PD

UI shows list of outgoing messages

Allows editing PD payload:

Hex editor or signal-based values

Set/clear bits or fields



Incoming PD

Display-only payload/fields

No editing allowed



MD (Message Data)

Support for all MD types

Outgoing MD:

Enter payload

Send request

View response in UI


Incoming MD:

Show sender IP, timestamp, payload



5. Logs

TRDP logs (PD + MD)

Application logs (errors, events)

Stored per user in SQLite



---

📁 Repository Folder Structure (Recommended)

/
├── backend/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── http/
│   │   ├── auth/
│   │   ├── db/
│   │   ├── trdp/
│   │   └── util/
│   ├── include/
│   └── third_party/
│       ├── httplib/
│       └── sqlite/
│
├── frontend/
│   ├── (React or HTML/JS code)
│
├── docs/
│   └── API_SPEC.md
│
└── README.md

Codex should follow this structure when creating new files.


---

📚 Database Schema (SQLite)

users

id (PK)
username TEXT UNIQUE
password_hash TEXT
role TEXT (admin/dev)
created_at DATETIME

xml_configs

id (PK)
user_id (FK)
name TEXT
xml_content TEXT
validation_status TEXT
created_at DATETIME

active_config

id = 1 (constant)
xml_config_id (FK)

trdp_logs

id (PK)
direction TEXT   (IN/OUT)
type TEXT        (PD/MD)
msg_id INTEGER
src_ip TEXT
dst_ip TEXT
payload BLOB
timestamp DATETIME

app_logs

id (PK)
level TEXT
message TEXT
timestamp DATETIME


---

🔌 REST API Overview

Auth

POST /api/auth/login

POST /api/auth/register

POST /api/auth/logout


TRDP Configurations

GET /api/trdp/configs

POST /api/trdp/configs (upload XML)

GET /api/trdp/configs/{id}

POST /api/trdp/configs/{id}/activate


Network

GET /api/network/config

POST /api/network/config


PD Communication

GET /api/pd/outgoing

GET /api/pd/outgoing/{id}

POST /api/pd/outgoing/{id}/payload

GET /api/pd/incoming


MD Communication

POST /api/md/send

GET /api/md/incoming

GET /api/md/outgoing


Account Management

GET /api/account/me

POST /api/account/me/password

GET /api/account/users (admin only)

POST /api/account/users/{id}/reset_password



---

🚀 Build Instructions

Backend

cd backend
mkdir build && cd build
cmake ..
make -j
./trdp_app

Frontend (React)

cd frontend
npm install
npm run dev


---

🔗 Integrating the TCNopen Fork as a Submodule

The [aloktj/TCNopen](https://github.com/aloktj/TCNopen) fork provides the TRDP implementation and Wireshark plugin we rely on. Add it to this repository as a git submodule (for example under `external/TCNopen`) so that both the backend and tooling can share the same source tree:

```sh
git submodule add https://github.com/aloktj/TCNopen.git external/TCNopen
git submodule update --init --recursive
```

After the folder is present, wire it into the backend build by adding an `add_subdirectory(external/TCNopen/trdp)` (or a similar wrapper CMakeLists) inside `backend/CMakeLists.txt`. Export the `trdp`, `tau`, and optional `trdp_spy_plugin` targets so the application executable can simply `target_link_libraries(trdp_app PRIVATE trdp tau)` without caring about the TRDP source layout.

#### Building the TRDP stack with CMake presets

The fork ships a comprehensive `CMakePresets.json`. Keep the TRDP build isolated under the submodule directory and use the upstream presets:

```sh
cd external/TCNopen
cmake -S . -B build/LINUX_X86_64 --preset LINUX_X86_64
cmake --build build/LINUX_X86_64
```

If you target a different architecture or compiler, start from the generic preset and override the cache variables that describe the platform:

```sh
cmake -S . -B build/custom --preset base \
  -DTRDP_TARGET_ARCH=<arch> \
  -DTRDP_TARGET_OS=<os> \
  -DTRDP_TARGET_VOS=<vos>
```

Every preset expects CMake ≥ 3.16, Ninja, and the dependencies listed in the TCNopen README (`uuid-dev`, `libglib2.0-dev`, FlexeLint, Doxygen, Graphviz, and the Debian packaging toolchain when needed). Append extra cache definitions such as `-DTRDP_MD_SUPPORT=ON` or `-DTRDP_DEBUG=ON` directly to the configure command when you need features like MD messaging, TSN, or the high-performance indexed mode.

#### Optional helper targets

You can keep using the upstream helper targets from inside `external/TCNopen`:

```sh
# Debian packaging
cmake --build build/LINUX_X86_64 --target bindeb-pkg

# Wireshark TRDP-SPY plugin
cmake --build build/LINUX_X86_64 --target trdp_spy_plugin

# Documentation and lint
cmake --build build/LINUX_X86_64 --target doc
cmake --build build/LINUX_X86_64 --target lint
```

The `.deb` packages and plugin artifacts are created inside the chosen build directory (for example `build/LINUX_X86_64/pkg` for packages and `build/LINUX_X86_64/trdp/spy/src/trdp_spy` for the plugin). Install the Debian packages with `sudo dpkg -i build/LINUX_X86_64/pkg/*.deb` and copy the plugin into Wireshark’s extension folder. The helper targets re-expose the upstream `debuild`, documentation, lint, and distribution workflows, so no legacy Makefiles are required.

Once the TRDP libraries are compiled, point the backend’s CMake toolchain to the install location or use `add_subdirectory` so that `trdp_app` links the freshly built static libraries directly. This keeps the TRDP stack synchronized with the application source tree and makes reproducible builds trivial on CI.

---

🌐 Running the Application

1. Start the backend:

./trdp_app


2. Access via browser:

http://<machine-ip>:8080



Multiple machines in same LAN can:

Run trdp_app

Exchange TRDP PD/MD messages with each other



---

🤖 Using This Repo With ChatGPT Codex

Codex should:

Generate CMake-compatible backend code

Use cpp-httplib for REST API

Use SQLite for all persistent data

Implement all TRDP communication using provided TRDP static library

Generate React components for each page

Follow the folder structure defined above

Always refer to this README while extending the project


When prompting Codex:

Always mention which file you want to generate/update

Paste relevant sections of code

Reference specific parts of this README



---

🗂️ Languages, Tooling, and Git Hygiene

This repository intentionally mixes several ecosystems, so the `.gitignore` (checked in at the
root of the repo) is tuned to keep transient files from each toolchain out of future pull
requests. The major languages and generators we rely on today are:

* **C & C++** – the backend, TRDP integration, and CMake-based build glue. Ignore object files
  (`*.o`, `*.obj`), archives (`*.a`, `*.lib`), shared libraries (`*.so`, `*.dll`, `*.dylib`),
  and entire CMake build directories (`build/`, `cmake-build-*`, `CMakeFiles/`).
* **CMake** – project orchestration for both `trdp_app` and the `TCNopen` submodule. Ignore
  generated presets (`CMakeUserPresets.json`), caches (`CMakeCache.txt`), helper metadata such
  as `compile_commands.json`, and the `external/TCNopen/build/` tree that hosts preset builds.
* **TypeScript / JavaScript (React)** – the web frontend. Ignore `node_modules/`, bundler output
  like `build/` or `dist/`, coverage folders, lockfiles, and framework-specific caches (`.next/`,
  `.vite/`, `.svelte-kit/`).
* **SQLite databases & runtime logs** – the backend creates `*.db`, WAL, and log files; they must
  never be committed.
* **Docs & packaging artifacts** – Doxygen output, Debian packages from the TRDP stack, or any
  ad-hoc archives should stay ignored (`/docs/`, `*.deb`, `*.tar.gz`, etc.).
* **Editor/tool state** – IDE folders (`.vscode/`, `.idea/`), `clangd` cache, swap files, and
  `.env` secrets are all excluded.

If you add another language or framework (for example Python helper scripts or a new frontend
stack), extend `.gitignore` alongside the code so that only human-authored sources enter the
history.


---

📌 Future Enhancements (Optional)

WebSocket live streaming for PD/MD updates

Role-based access control

Ability to export/import entire user profile

XML schema visualizer

TRDP simulation mode without real network
