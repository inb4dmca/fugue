# account

Account and identity handling for the pager and client. No sockets; Qt6::Core only. Builds the library `fugue_account`, the helper `fugue-account` and the `account_*` tests.

| file | role |
|---|---|
| `store.*` | on-disk formats under `~/.fugue` and key.txt discovery |
| `keyblock.*` | key.txt parsing |
| `desc.*` | password-hash helpers |
| `records.*` | login record and hub-link helpers |
| `login.*` | local sign-in helpers |
| `tool/main.cpp` | `fugue-account`: one-line JSON commands over the above |

Rules:
- Tests repoint `HOME` at a temp directory and never touch a real `~/.fugue` or `~/.byond`.
- `fugue-account` takes secrets on stdin, never argv.
- Client-only state goes in `~/.fugue/qt/` (`qtStateDir()`), not `pager.json`.
- Saves are atomic (temp, fsync, rename) and mode 0600.
- POSIX only (`glob(3)`, `rename(2)`, `mkstemp`).
