# Source versions

The original public version at `b6fe9e2` used fixed K/n_probe/global-m settings.
It remains in Git history. The modular static implementation and its input,
execution and measurement records were published at `ac17836`.

The current review removes source comments and updates documentation. Comment
removal alone was checked by recompilation: both Release binaries had SHA-256
`32085fbbb20e710a71f4a58d43ea8e262e422ff086415a17f595ce8e2b6f8265`.
Additional functional changes and their measurements are recorded in
[VERIFICATION.md](VERIFICATION.md).

Historical measured-result files retain their measured source and binary hashes.
Source revisions do not retroactively change recorded query times or metric
values. Public static tasks use the same implementation as the core CLI.
