# pager

The launcher: account sign-in, server list, and joining a game by re-executing the binary as `fugue-qt client --connect ...`. The logic is Qt6::Core only; the window is Qt Widgets.

| file | role |
|---|---|
| `listing.*`, `target.*`, `text.*` | hub listing parser, `byond://` / `host:port` target parser, text helpers |
| `dsargv.*` | DreamSeeker's command-line grammar, for the `dreamseeker` name |
| `model.*` | status, checklist and the `-list` text |
| `signin.*`, `hublink.*` | username/password sign-in against the hub (one blocking connection with a deadline) |
| `hubcert.*` | login-certificate token for hub-registered servers |
| `clientaccount.*` | which account a client launch plays as |
| `cache.*` | what Preferences > Clear cache removes and keeps |
| `launch.*` | join argv builder (no secrets), scrubbed child environment, headless join, the guest and auto-sign-in markers, `.desktop` install |
| `ui/` | the pager window; every glyph is drawn with QPainter |
| `tool/` | `fugue-pager` helper and `fugue-pager-shot` (offscreen screenshots with a temp HOME) |

How the launcher behaves for a player (modes, state files, environment) is in [docs/USAGE.md](../docs/USAGE.md); how
credentials are handled is in [SECURITY.md](../SECURITY.md).
