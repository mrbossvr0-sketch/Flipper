# PrivateLab MNTM 3.1 — Momentum Firmware Edition

A native Flipper Zero app project for **Momentum** custom firmware, with a **GitHub Actions workflow** that compiles an installable `.fap` using **Momentum's SDK index**. This is an app, **not** a firmware flash image.

![Concept UI preview from PrivateLab 3.0; not a hardware screenshot](preview/ui_preview.png)

## 🚀 Build it on GitHub without installing compilers

1. Create a GitHub repository (private or public); choose **Add README: off**, if possible, to upload all files in one go.
2. Extract this ZIP. Upload **the contents of its top-level directory** to the repository, including the hidden `.github` directory. An easy option for all files is `git init`, `git add .`, commit, then push, or drag and drop all files via GitHub's **Add file → Upload files**. Check that GitHub shows `.github/workflows/build-momentum.yml` afterward.
3. Go to the repository's **Actions** tab → **Build PrivateLab for Momentum** → **Run workflow**.
4. Choose **release** if your Flipper uses the current Momentum stable firmware; choose **dev** if it has Momentum dev firmware. GitHub will run C syntax tests, host logic tests, fetch the corresponding **Momentum** SDK, compile the app, and upload the resulting `.fap` if compilation succeeds.
5. Open the completed green run. Under **Artifacts**, download `PrivateLab-Momentum-...`, unzip, and take `private_lab.fap`.
6. Using qFlipper, copy `private_lab.fap` to **SD Card → apps → Tools**. On the device open **Apps → Tools → PrivateLab MNTM**.

The workflow runs automatically on relevant pushes and pull requests, and also supports manual runs. No GitHub token or secret is required for builds. Upload artifacts are retained 30 days by default.

**IMPORTANT:** `.fap` compatibility is checked against the actual Momentum SDK used in CI, but the SDK version must also match the version installed on your Flipper. A successful compiler run cannot prove GPIO or RF behavior on physical hardware.

## 🧰 What is really implemented

| Feature | Actual behavior | Hardware |
|---|---|---|
| Custom UI | Home dashboard, button-driven tool/settings menus | Built-in |
| Sub-GHz RF meter | Receive-only RSSI, presets, sweep, peak/trend | Built-in CC1101 |
| Digital GPIO | Reads PC3 (3.3V input), pin 7 | Own low-voltage circuit |
| Voltage meter | ADC PC0, **0–2.5 V** input, pin 16 | Own conditioned input |
| UART monitor | Receives serial data, adjustable baud | 3.3V UART adapter |
| OBD-II | Read-only Mode 01 engine speed, road speed, coolant temperature | Isolated ELM327-compatible **TTL UART** hardware |
| System | Power/battery, device info, log status | Built-in |
| Logs & settings | Settings persistence and TSV evidence logging on microSD | microSD |
| Guided checks | NFC, RFID, iButton, IR, BLE, Wi-Fi accessory, vehicle fobs, USB/GPIO checklists | Some built-in apps or peripherals; **guides, not integrated tools** |
| Desktop report | Read TSV log, build Markdown report and hashes | PC |

The RF meter is a **signal-strength** receiver, not a packet decoder or calibrated spectrum analyzer. No RF replay, jamming, key capture, car unlocking, ECU programming, bypasses or HID payloads are included. Wi-Fi requires additional hardware; there is no onboard Wi-Fi radio.

## 💻 Build locally (optional)

Install Python and uFBT:

```bash
python3 -m pip install --upgrade ufbt
./build.sh release  # for stable Momentum
# OR: ./build.sh dev  # for dev firmware
```

The script runs (in `applications_user/private_lab/`):

```bash
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel=release
ufbt
```

If it compiles, the app is at `applications_user/private_lab/dist/private_lab.fap`. This is **not** an official-flipper SDK build. If you already use uFBT for other firmware, point `UFBT_HOME` to a separate directory first to keep Momentum and stock SDKs separate.

## 🧪 CI and testing

- `./test_host.sh`: C syntax against local **mock** headers, OBD parser logic, and desktop report tests. This is only a preliminary check.
- `.github/workflows/build-momentum.yml`: real uFBT compile against the selected Momentum SDK, checks that the produced FAP exists and is non-empty, uploads `.fap` plus SHA-256 checksum.
- No physical Flipper is available in the build environment. Test carefully on owned low-voltage hardware.

## ⚠️ Important hardware safety

- Never connect Flipper GPIO directly to mains, CAN bus, 12-V automotive wiring, 5-V signals or unknown voltages. Digital GPIO is **3.3 V**. This ADC configuration accepts **0–2.5 V** only.
- The OBD-II feature requires a **safe, isolated ELM327-compatible interface that exposes 3.3-V TTL UART**. A normal Bluetooth ELM327 dongle is not directly usable from this UART code. Use only with the vehicle safely parked.
- Sub-GHz tooling is receive-only. Any external RF hardware must comply with your local rules.

## 🔧 Problems and fixes

- **Actions workflow missing:** Check `.github/workflows/build-momentum.yml` exists in the repository (not hidden inside an extra directory). It must be committed to the default branch for manual dispatch.
- **Red X on host tests:** Open **Host tests and source checks** logs; fix errors there before expecting an app.
- **Red X on Momentum build:** Download the log from the failed **Build using Momentum firmware SDK** step. Missing symbols/API changes indicate code needs to be adapted to your selected SDK; change source or select the matching firmware channel. CI won't fabricate a `.fap` on failure.
- **Error when opening FAP on the Flipper:** SDK/API version mismatch is likely. Rebuild with the corresponding Momentum SDK channel/version for your exact firmware. The workflow's artifact suffix includes the SDK/API build information.
- **UART / OBD not showing live data:** An external physical adapter and safe wiring are required; it cannot communicate directly with a car without them.

## License / provenance

GPL-3.0-only (see `LICENSE`). Independent, unofficial application. Based on the prior PrivateLab 3.0 code; the Momentum 3.1 edition adds source targeting, SDK-specific build scripts, and Actions setup. Momentum is maintained by Next-Flip: https://github.com/Next-Flip/Momentum-Firmware . GitHub Actions uses the public [flipperdevices/flipperzero-ufbt-action](https://github.com/flipperdevices/flipperzero-ufbt-action) release `v0.1` and the Momentum SDK index.

*Status: host tests completed in this environment. The Actions workflow has not been run from a user's GitHub repository, so an install-ready Momentum FAP cannot yet be guaranteed.*
