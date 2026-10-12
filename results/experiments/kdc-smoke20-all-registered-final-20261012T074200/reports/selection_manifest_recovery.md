# Selection manifest recovery

The persisted selector JSON was removed when a resume attempted to rebuild the canonical dataset directory. The manifest has been reconstructed using the unchanged source dataset and the smoke20 selector settings. All 20 canonical paths and SHA-256 checksums match the frozen `selected_files` entries in `experiment_manifest.json`.

- Reconstructed path: `dataset/selection_manifest`
- Reconstructed file SHA-256: `10750510381034c92cda6ce81b9196697b4e057aa9af864c8445373ef66b4877`
- Previously recorded selector fingerprint: `89a8af7f1b7c7fd4b1e95c584e87dcc8d75f65e74ce8179fd010a3272c131b3d`
- The selector embeds creation-time and materialization metadata, so the reconstructed file is semantically equivalent for selected inputs but is not byte-identical to the lost selector output. The prior fingerprint remains recorded as historical evidence.
- The source dataset was not modified.
