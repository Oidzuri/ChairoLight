# ChairoLight release test matrix

Record the tester, date, GPU/driver, monitor model, Arduino model, LED count and result for every row. A release candidate must not be marked stable while any P0/P1 row is untested or failing.

Status values: `PASS`, `FAIL`, `BLOCKED`, `NOT RUN`.

Local build environment observed on 2026-07-15: Windows build 26100 (Windows 11 24H2 generation; the compatibility registry may label it Windows 10), two detected monitors, 2560×1440 primary display and 100% scaling. This records the environment only; it does not turn the unperformed HDR, DPI, sleep or hardware rows below into passes.

## Automated baseline

| Priority | Test | Expected result | Status |
|---|---|---|---|
| P0 | Clean Release build | No compiler/linker errors | PASS (local, 2026-07-15) |
| P0 | Unit/API test suite | Every test class passes | PASS (local, 2026-07-15) |
| P0 | Portable privacy audit | Empty portable marker; no profile, COM port or user path | PASS (scripted) |
| P1 | WGC black BGRA regression | RGB output remains 0/0/0 | PASS (unit test) |

## Windows and display

| Priority | OS / configuration | WGC SDR | DD SDR | WGC HDR | DD HDR | Status |
|---|---|---:|---:|---:|---:|---|
| P0 | Windows 11, one monitor, 100% DPI | required | required | required | required | NOT RUN |
| P0 | Windows 10 22H2, one monitor, 100% DPI | required | required | required | required | NOT RUN |
| P1 | Windows 11, 125% DPI | required | required | optional | optional | NOT RUN |
| P1 | Windows 11, 150% DPI | required | required | optional | optional | NOT RUN |
| P1 | Two monitors, same DPI | required | required | optional | optional | NOT RUN |
| P1 | Two monitors, mixed DPI | required | required | optional | optional | NOT RUN |
| P1 | Hybrid GPU laptop | required | required | optional | optional | NOT RUN |

For every capture cell check: neutral black, neutral gray, white, red, green, blue, pink/magenta, video motion, window resize, monitor switch and 10 minutes without freezes.

## Lifecycle and hardware

| Priority | Scenario | Expected result | Status |
|---|---|---|---|
| P0 | Start with Windows | Starts in tray, no main window flash | NOT RUN |
| P0 | Tray → Settings | Main window opens and becomes active | NOT RUN |
| P0 | Arduino unplug/replug | Connection and output recover without restart | NOT RUN |
| P0 | PC sleep/resume | App remains alive and reconnects | NOT RUN |
| P1 | Display off/on | Capture resumes without permanent black frame | NOT RUN |
| P1 | Lock/unlock session | Respects keep-lights setting and recovers | NOT RUN |
| P1 | COM number changes | Clear error; selecting new port recovers | NOT RUN |
| P1 | 30-minute bright-scene run | No runaway white/yellow wall wash or disconnect | NOT RUN |

## Release package

| Priority | Check | Expected result | Status |
|---|---|---|---|
| P0 | Extract on clean Windows user | App launches with bundled Qt runtime | NOT RUN |
| P0 | First launch | No author profile, COM8 or personal paths | scripted PASS / manual NOT RUN |
| P0 | LICENSE and NOTICE | Both present in archive | scripted PASS |
| P1 | SmartScreen | Signed publisher shown, or unsigned warning documented | NOT RUN |
| P1 | VirusTotal/reputation check | No confirmed detection | NOT RUN |

## Sign-off

- Release candidate:
- Commit:
- Tester(s):
- Date:
- Remaining accepted limitations:
- Decision: `GO` / `NO-GO`
