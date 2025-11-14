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

📌 Future Enhancements (Optional)

WebSocket live streaming for PD/MD updates

Role-based access control

Ability to export/import entire user profile

XML schema visualizer

TRDP simulation mode without real network
