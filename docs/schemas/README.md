# Experiment schema contracts

These JSON Schemas use Draft 2020-12 and define version-1 documents for
experiments, canonical dataset instances, expected run-plan entries, immutable
run records, verification results, artifact manifests, and structured trace
events. `scripts/kdc_tools/schemas.py` loads only the registered schemas,
checks the schema documents themselves, rejects non-standard JSON constants,
and validates documents with format checking.

The version belongs to the document (`schema_version`), not the filename.
Incompatible semantic changes require a new document version and an explicit
compatibility reader; do not weaken strict schemas to accept historical
records. Optional or unavailable numeric measurements use `null`, never
NaN/Infinity. A verification kind distinguishes empirical checks from
continuous certification; it does not imply success by itself.

`scripts/kdc_tools/storage.py` can publish schema-validated JSON as a
create-only artifact and compute its SHA-256 digest. Existing C++ and Python
result producers are not yet migrated to these schemas, so these contracts
currently govern the new storage foundation only.

Run the focused contract tests with:

```sh
pytest -q tests/python/test_schemas.py tests/python/test_storage.py
```
