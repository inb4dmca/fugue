# Usage

## Commands

```bash
fugue-qt                              # release build: the launcher window (server list, sign-in)
fugue-qt pager byond://host:port      # the launcher with a server preselected
fugue-qt pager -join host:port        # headless: join and stream the client's log
fugue-qt client --connect host:port   # open the game window directly (as a guest)
fugue-qt --help                       # options (also `pager --help`, `client --help`)
fugue-qt --version                    # prints e.g. "fugue-qt 0.5.0" (accepted by every mode; also `fugue-qt version`)
```

On macOS, use `Fugue.app/Contents/MacOS/Fugue` in place of `fugue-qt`, or open the app from Finder.

* `pager -list` prints the saved server list and a readiness checklist. `pager -dry-run -join host:port` prints the
  command that would run.
* `pager -warm -join host:port` downloads the world's whole resource list into the cache and exits. The launcher
  window has the same action, **Download resources**.
* `pager -login` signs in with a BYOND account. The password comes from `$FUGUE_BYOND_PASSWORD` or a prompt and is
  stored nowhere. The launcher window has the same sign-in.
* `pager -install-desktop` adds Fugue to the application menu and makes it the handler for `byond://` links. It
  writes only under `$XDG_DATA_HOME` (default `~/.local/share`). This command is Linux-only; on macOS, copy `Fugue.app`
  to Applications and open it to register `byond://` links.
* Installed or symlinked under the name `dreamseeker`, the binary accepts DreamSeeker's own command line, for example
  `dreamseeker "byond://host:port" -pager`, so existing launch scripts and `byond://` handlers work.
* Guests and signed-in players both work; signing in lets servers that require a BYOND key accept you.
* A game started from the launcher, or as `dreamseeker`, plays as the signed-in account. `client --connect`,
  `--state-dir` and `--screenshot` runs play as a guest unless `--account` names one.
* Logout keeps the saved sign-in (`identity-<key>`); **Preferences > Clear cache** removes saved sign-ins, the
  resource cache and the login-certificate cache, and keeps `pager.json` and the machine id.

## For server operators

Fugue announces itself in two ways. Its `client.computer_id` always satisfies `text2num(computer_id) % 1000 == 523`, and
`winget(src, null, "fugue-version")` answers `fugue-qt <version>` (a development build answers `fugue-qt <version>-dev`),
where stock DreamSeeker answers the empty string. `winget` waits for the client's reply, so call it from a `spawn()`:

```dm
/client/New()
	. = ..()
	spawn()
		var/v = winget(src, null, "fugue-version")
		if(v)
			log_admin("[key] is using [v]")
```

Both are disclosure, not enforcement (see [SECURITY.md](../SECURITY.md)).

## State directories

All under `$HOME` (on both Linux and macOS):

| Path | Contents |
|---|---|
| `~/.fugue/` | `pager.json` (server list, settings), `identity-<key>` (cached sign-ins), `key.txt`, `byond-sid` and `sid` (machine ids; `sid` is the one servers see, and lives under `$XDG_STATE_HOME/fugue/` when that is set), `cauth.txt` (login-certificate cache), `macros.dmf` and `macros/<host>_<port>/macros.dmf` (global and per-server user macros), `qt/` (launcher flags such as the remembered guest choice), `rsc/` (the shared resource cache), `audio.log`, `client.log` |
| `~/.byond/cfg/key.txt` | an existing BYOND key, imported at sign-in. If that file exists, sign-in updates it in place. |

## Environment

A command-line flag, where one exists, wins.

| Variable | Meaning |
|---|---|
| `FUGUE_AUDIO` | audio sink; same as `--audio` |
| `FUGUE_IDENTITY` | path of the identity file to log in with |
| `FUGUE_PAGER_CONFIG` | path of `pager.json` |
| `FUGUE_BYOND_SID` | path of the `byond-sid` file |
| `FUGUE_KEYTXT` | path of the `key.txt` to use |
| `FUGUE_CLIENT` | client binary the launcher starts (default: itself) |
| `FUGUE_BYOND_PASSWORD` | sign-in password for `pager -login`; read from the environment only and never passed on to the client |

## Audio

`--audio` or `FUGUE_AUDIO` selects the sink:

* `auto` (default): Core Audio on macOS; on Linux, tries `pw-play`, then `paplay`, then `aplay`
* `none`
* `wav:<path>`: write a WAV file instead of playing
* `cmd:prog,arg,...`: pipe raw signed 16-bit little-endian stereo PCM to a command

Every `[audio]` line (which sink opened, why earlier candidates failed, refused files) is also written to `audio.log`
in the state directory, which helps when Fugue is started from a menu with no terminal.
