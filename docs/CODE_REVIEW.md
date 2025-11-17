# Cash-Sloth Code Review Notes

These observations highlight potential risks, edge cases, or maintenance items noticed
during a quick pass through the codebase.

## Runtime platform and build guard
- The current `main` function immediately exits with an error on non-Windows platforms,
  which is expected for a Win32 app but makes cross-platform CI impossible without a
  Windows runner. Consider adding a stub harness for headless checks (formatting, static
  analysis) so Linux CI can still catch regressions early. 【F:src/main.cpp†L1-L115】

## Catalogue loading and validation
- `Catalogue::loadFromFile` treats any parsing or validation failure as a silent `false`
  return and only logs to `stderr`, so the UI ultimately falls back to the default
  catalogue without surfacing the error path. Surfacing the failing filename and reason
  in the UI banner (not just the console) would help operators diagnose broken
  deployments. 【F:src/main.cpp†L90-L120】【F:src/main.cpp†L1622-L1645】
- The parser accepts both keyed objects and `categories` arrays, but discards any article
  lacking a valid name and price. If a catalogue JSON accidentally drops a field, the UI
  simply shows fewer products. Adding a warning count for skipped articles could make
  data issues more obvious. 【F:src/main.cpp†L123-L199】【F:src/main.cpp†L199-L275】

## User input and messaging
- Manual credit entry relies on `parseAmount` to normalise strings (trimming, replacing
  commas) and then rejects non-positive values, which is good for kiosk use. However, the
  code uses a fixed 64-character buffer for reading the input control; extremely long
  pasted strings get truncated silently. Using the control length or clamping input to a
  friendlier limit (and messaging the user) would improve feedback. 【F:src/main.cpp†L1817-L1829】

## Testing gaps
- There are no automated tests or sanity checks for JSON parsing, layout calculations, or
  Win32 message handling. Adding lightweight unit tests around the JSON parsers (e.g.,
  `parsePrice`, catalogue validation) would guard against regressions when adjusting the
  schema. 【F:src/main.cpp†L176-L199】【F:src/main.cpp†L200-L275】
