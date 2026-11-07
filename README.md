# NetMeIn — LG NetCast 4 privilege escalation (GET OFF MY TV)

Root exploit for LG NetCast 4 (mt5398, kernel 3.0.13, armv7l) with a
full-screen SDL debug-log UI.

It scans physical RAM via `/dev/mem` for the running process's `struct cred`
(uid/gid 502 = `browser` / 500 = `game`), zeroes it → the process becomes
root. On success it installs a minimal static setuid-root `su` in
`/home/su` (chmod 4755) and writes `/mnt/cache/browser/extra_conf` to launch
`telnetd -p 2323 -l /home/su &`, so after reboot you connect to a root shell.

## Files

| File                | Purpose                                            |
|---------------------|----------------------------------------------------|
| `netmein.c`         | Exploit + SDL(nc4) GUI + persist logic             |
| `su.c`              | Minimal static setuid-`su` → root `/bin/sh -i`     |
| `font8x8_basic.h`   | 8x8 bitmap font (public domain, dhepper/font8x8)   |
| `nc4log.h`          | Logging helper for the NetCast 4 app environment   |
| `build.sh`          | Cross-compile script (armv7l)                      |

## Notes

- `netmein` is linked against a **statically built** SDL2 with the `nc4`
  video driver (see `SDL-webOS-nc4`) plus the dynamic `liblgncopenapi.so.2`
  from `webos-userland-nc4-support`.
- It is meant to be launched through the app manager (`nc4app run`) so SDL
  window actually appears on the TV.
- The setuid `su` is embedded into `netmein` at build time via `objcopy`.

## Disclaimer

For own-device research only.

## License

Font: public domain (Daniel Hepper). Everything else: MIT.