# Public Copy Notes

This folder was generated as a public-safe copy of the project.

Applied cleanup:
- Copied only git-tracked source files from the current working tree (no `.git` history, no ignored local runtime data).
- Removed workspace-local folders: `.vscode/`, `.qodo/`.
- Removed `.DS_Store` files.
- Replaced local absolute path references with `<repo-root>` in docs.

Before publishing, run one more secret scan in this folder:

```bash
rg -n --hidden -S "seed phrase|mnemonic|private key|BEGIN .*PRIVATE KEY|api[_-]?key|token|secret|password" .
```
