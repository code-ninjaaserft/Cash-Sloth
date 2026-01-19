# Cash-Sloth Wiki

This document can be used as a seed for a GitHub Wiki or shared internally to onboard
contributors and operators.

## Overview
- **Goal:** Touch-first Windows POS prototype with a JSON-driven catalogue and theming.
- **Tech stack:** Win32 + GDI for rendering, custom JSON parser, no external runtime
  dependencies beyond the Windows SDK and a C++17 compiler.

## Getting started
1. Clone or download the repository on Windows.
2. Install MinGW-w64 or Visual Studio (Desktop development with C++ workload).
3. Build via `mingw32-make` (MinGW) or the CMake generator of your choice. See
   [README.md](../README.md) for details.
4. Keep the `assets/` directory next to the produced executable so styles, catalogue, and
   imagery are located at runtime.

**Quick build reference**
- CMake configure: `cmake -S . -B build`
- CMake build: `cmake --build build --config Release`

## Asset conventions
- **Catalogue:** `assets/cash_sloth_catalog.json` defines categories, articles, prices,
  and optional barcodes. The parser also accepts a `categories` array or a top-level
  object keyed by category name.
- **Style sheet:** `assets/style.json` drives palette colors, typography, spacing, quick
  payment amounts, and hero text.
- **Images:** `assets/cash_sloth_images.json` provides metadata for bundling artwork; it
  is optional but helps keep binary releases organised.

## Troubleshooting
- The app shows the default catalogue and style if assets are missing or invalid. Check
  the console output for parsing warnings (e.g., missing fields) when assets fail to load.
- Non-Windows systems will exit immediately because the UI is entirely Win32 based.
- If touch targets look too small, adjust `metrics` values in `assets/style.json` (e.g.,
  `quick_button_height`, `margin`, `tile_gap`).

## Roadmap

### Milestones Overview
- **Milestone: QEN-GV (Mitte Februar)** — _“Event-ready Core”_
  - Ziel: CashSloth.cpp im Event stabil einsetzen, auch ohne Barcode.
- **Milestone: Z’Ämme ässe (August)** — _“Presets + Cloud + Barcode (Scanner+Android)”_
  - Ziel: Presets bauen/teilen, am Kollegen-Laptop laden, und Barcode via echtem
    Scanner oder Android (Bluetooth, mit WLAN-Fallback).

### Milestone 1: QEN-GV (Feb) — Issue Liste

#### Core Stabilität & UX
- **[CORE] Transaction State Machine härten**
  - Klarer Zustand: Idle / InSale / Payment / Completed
  - Keine “halben” Zustände nach Fehlern
- **[UI] Safe-Guards für Event**
  - Pay deaktiviert wenn Warenkorb leer
  - Optional: Bestätigung bei “Clear cart”
  - Klarer Error-Toast statt Messagebox-Spam (falls du Toast schon hast)
- **[DATA] Robust JSON load + Fallback**
  - Wenn Catalog/Style fehlt oder kaputt: Fallback + klare Meldung
  - “Safe mode” default-Katalog (minimal) optional
- **[LOG] Transaction CSV Log (nur nach erfolgreicher Zahlung)**
  - Timestamp, Total, Paid, Change, Items (kompakt)
  - Rotating/File pro Tag (z.B. `sales_YYYY-MM-DD.csv`)
- **[PERF] Input/Rendering Smoothness pass**
  - Keine UI-Lags bei schnellem Tippen/Klicken
  - Optional: weniger Repaints / double-buffer checks (falls nötig)

#### Event-Quality-of-Life (optional, wenn Zeit)
- **[QOL] Undo last action (Cart)**
  - Undo Add/Remove (1 Schritt reicht)
  - (Kein Barcode nötig)
- **[QOL] Quick-Amounts & Cash Buttons polish**
  - Quick buttons konfigurierbar (lokal)
  - “Undo last cash add” stabil

**Definition of Done Feb:** 100+ Verkäufe am Stück ohne Stress, mit Log-Datei, ohne Barcode.

### Milestone 2: Z’Ämme ässe (August) — Issue Liste (Presets + Sharing + Barcodes)

#### A) Presets lokal (Event-Pakete)
- **[PRESET] Preset folder format einführen**
  - `presets/<presetId>/meta.json`
  - `presets/<presetId>/catalog.json`
  - optional `style.json`, `quickAmounts.json`
  - meta: name, author, version, description, updatedAt, hash
- **[PRESET] Preset Manager UI**
  - Preset auswählen (Startscreen oder Settings)
  - “Import/Export preset” (ZIP oder Ordner)
- **[PRESET] Preset validation**
  - Duplicate barcodes check (wichtig für später)
  - Preisvalidierung, leere Namen etc.
- **[PRESET] Preset versioning & hashing**
  - Hash (z.B. SHA-256 über Catalog JSON)
  - Version bump optional automatisch

#### B) Cloud-Sharing (SQL synchronisiert, aber via API)
- **[CLOUD] Backend API Spec definieren**
  - Endpoints: list, download, upload
  - Auth: Token oder Share-Code
  - Response: meta + preset payload
- **[CLOUD] Preset Server (API) + SQL Storage**
  - DB tables: presets, preset_versions, users (minimal)
  - Store: meta + blob (zip/json) + hash
- **[CLOUD] CashSloth.cpp Cloud Client**
  - Preset Browser (list)
  - Download preset → in `presets/` speichern
  - Upload preset → vom lokalen Preset
- **[CLOUD] Permission model (MVP)**
  - public / unlisted (share code) / private (optional später)
  - Rate limit / basic abuse protection (light)

#### C) Barcodes (alles hier in August)
**Gemeinsamer Kern (wichtig: 1 Pipeline, viele Inputs)**
- **[BARCODE] ScanPipeline implementieren**
  - `OnBarcodeScanned(code, source)`
  - sanitize/normalize
  - lookup barcode -> article
  - add to cart + UI feedback
  - unknown handling (toast + optional beep)
- **[BARCODE] Catalog barcode index**
  - Beim Laden: Map bauen
  - Doppelte/ungültige Barcodes melden (Validation hook)

**Echter Barcode-Scanner (Keyboard-Wedge)**
- **[BARCODE] Keyboard-Wedge Scanner Adapter**
  - Buffer bis Enter/Timeout
  - “zu langsames Tippen” nicht als Scan werten (threshold)
  - Immer verfügbar (Fokus-unabhängig wenn möglich)

**Android als Scanner (Bluetooth primär, WLAN fallback)**
- **[BARCODE] Android Pairing UX**
  - Anzeige: verbunden/nicht verbunden
  - Pairing token/session concept (auch für BT)
- **[BARCODE] Android → Windows via Bluetooth (MVP)**
  - Wahl später: BLE oder SPP/COM (du kannst beides als follow-up)
  - Minimal: “send barcode string” + ACK/FAIL
- **[BARCODE] Android app MVP**
  - Scan (ZXing/ML Kit)
  - Auto-send oder send button
  - Feedback (vibrate/beep, server reply)
- **[BARCODE] WLAN/LAN Fallback Receiver in CashSloth.cpp**
  - Lokal HTTP/WebSocket endpoint (nur fürs Backup + Debug)
  - Token-geschützt
  - Nimmt code an → geht in ScanPipeline
- **[BARCODE] Integration Tests / Event Stress Tests**
  - “100 scans in 2 min” simulation
  - Unknown codes, duplicates, lag spikes
  - Logging für scan events (separat vom sales log, optional)

**Definition of Done August:**
- Preset zuhause erstellen → upload → Kollege lädt → nutzt am Event.
- Barcode geht entweder mit Hardware-Scanner oder Handy (BT), und WLAN rettet euch, falls BT zickt.

### Labels/Struktur fürs Repo (empfohlen)
- milestone:feb-qen-gv
- milestone:aug-zaemme-aesse
- area:core, area:ui, area:data, area:log, area:preset, area:cloud, area:barcode, area:android
- priority:P0/P1/P2
