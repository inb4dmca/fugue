# Security

* Fugue is unofficial and not affiliated with BYOND.
* It signs in to BYOND accounts the way DreamSeeker does, over BYOND's own undocumented mechanism. BYOND may change or
  block it at any time, and since it does not use TLS, Fugue is no safer than stock.
* The cached identity files (`identity-<key>`) and `key.txt` under `~/.fugue` act like credentials: anyone with a copy
  can sign in as you. Fugue writes them owner-only (mode 0600 on Unix; a protected owner/SYSTEM ACL on Windows). Keep them private and do not share or commit them. If
  an existing `key.txt` is found elsewhere, for example under `~/.byond/cfg`, sign-in updates that file in place, again
  owner-only. Windows resource-cache files inherit the user profile's ACL; do not put the state directory in a public share.
* Passwords are never put on a command line or in a log. They are read from the environment variable
  `FUGUE_BYOND_PASSWORD` or a prompt, used for one request, and wiped.
* Fugue marks its `computer_id` so server operators can recognise it (see
  [docs/USAGE.md](docs/USAGE.md#for-server-operators)). A modified build could drop the mark, so it is not a security
  guarantee.
