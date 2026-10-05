# OUI index

Manufacturer enrichment is an offline lookup. The firmware does not download a registry, and it does not parse the raw IEEE CSV while it scans.

The authoritative source is the public IEEE MA-L registry:

https://standards-oui.ieee.org/oui/oui.csv

Refresh the committed runtime artifact from a newly downloaded copy:

```text
python tools/oui/build_oui_index.py --self-test --csv path/to/oui.csv --out src/OuiData.gen.inc
```

The script keeps `Registry` values of `MA-L` only. Assignments may be `AABBCC`, `AA:BB:CC`, or `AA-BB-CC` in either hex case. Organization names keep letters, digits, spaces, and `. _ - & ' ( ) , /`, drop every other character, and store at most 64 characters. A duplicate assignment keeps the lexicographically smaller sanitized name. Malformed rows are skipped. Non-ASCII letters are dropped, and a name that becomes empty is omitted instead of being transliterated. The IEEE row `04208A` is omitted for that reason. The output is a sorted prefix table plus one shared name blob. `src/OuiData.cpp` compiles that file only when `WLS_OUI_EMBEDDED` is set. Without it, lookup stays empty and scanning still runs.

Do not commit the raw CSV, a passphrase, or a private registry export. The generated file records the SHA-256 of the CSV it was built from.
