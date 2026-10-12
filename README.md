# goodix-5117

A Linux driver for the Goodix `27c6:5117` fingerprint reader, written from
scratch in C — from raw USB packets to unlocking the desktop.

The sensor ships in several Honor and Huawei laptops. Mainline libfprint does
not support it, Goodix publishes no Linux driver, and its image data travels
over an encrypted channel, so it is unusable on Linux out of the box.

This repository makes it work.

```
    your finger
         │
    ┌────▼─────────────────────────────────────────────────────┐
    │  Goodix 5117 sensor                                      │
    └────┬─────────────────────────────────────────────────────┘
         │  USB bulk, endpoints 0x01 / 0x81
    ┌────▼─────────────────────────────────────────────────────┐
    │  libgoodix        framing, init, TLS-PSK, image, matching │
    └────┬─────────────────────────────────────────────────────┘
         │
    ┌────▼─────────────────────────────────────────────────────┐
    │  goodix5117       libfprint driver, worker thread         │
    └────┬─────────────────────────────────────────────────────┘
         │
      libfprint  ──►  fprintd  ──►  PAM  ──►  sudo, lock screen
```

## Status

Working end to end on an Honor MagicBook, Ubuntu 22.04, firmware
`GF_ST411SEC_APP_12117`.

| | |
| --- | --- |
| USB enumeration and framing | works |
| Initialisation sequence | works |
| Encrypted channel (TLS 1.2 PSK) | works |
| Image capture and decoding | works |
| Matching | works, by cross-correlation |
| libfprint driver | works |
| `fprintd` and PAM | works — `sudo` and the lock screen accept the finger |

Everything in [`SPEC.md`](SPEC.md) was measured on real hardware rather than
assumed, including the parts that contradicted the projects this work started
from.

## Before you start: the pairing key

The sensor talks over TLS with a pre-shared key, and **which key it holds
depends on whether Windows Hello ever touched it**.

| Your machine | What you need |
| --- | --- |
| Never enrolled a finger in Windows | nothing — the factory key is public and this code falls back to it |
| Windows Hello was used | extract the key once, under Windows |

A sensor paired by Windows keeps a key of its own, and nothing on the Linux
side can guess it. Recovering it means reading
`C:\ProgramData\Goodix\Goodix_Cache.bin` and decrypting it with DPAPI, in the
Windows session where the finger was enrolled. See
[`SPEC.md` section 6](SPEC.md) for the derivation, and the credits below for
the tool that implements it.

Rewriting the sensor's key instead is possible but requires erasing and
reflashing its firmware, with no way to back up the running image. This
project deliberately does not implement it.

The key goes in `clef.txt` at the repository root for the standalone tools, or
`/etc/goodix5117/psk` for the daemon. Both are gitignored. It is never printed
and is wiped from memory after use — a fingerprint cannot be changed once it
leaks, and neither can this key.

## Building

```sh
sudo apt install libusb-1.0-0-dev libssl-dev
make
make test
```

The test suite touches no hardware: it covers framing, image decoding,
matching and key parsing as pure functions, and runs in well under a second.

```
image           38 checks, 0 failed
match           16 checks, 0 failed
protocol        87 checks, 0 failed
session         10 checks, 0 failed
all tests passed
```

Two of those checks are worth singling out, because they are what separates
an algorithm bug from bad input data: an image correlated with itself must
score exactly 1.0, and so must a copy shifted within the search window. When
real fingerprints scored badly during development, those two passing said the
matcher was sound and sent the search towards the captures instead.

That gives you the standalone tools. Device access without root needs a udev
rule:

```sh
sudo tee /etc/udev/rules.d/60-goodix-fingerprint.rules <<'RULE'
SUBSYSTEM=="usb", ATTR{idVendor}=="27c6", ATTR{idProduct}=="5117", MODE="0660", GROUP="plugdev"
RULE
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb
```

### Tools

| Command | What it does |
| --- | --- |
| `build/enumerate` | dump the sensor's USB descriptors |
| `build/init` | run the initialisation sequence |
| `build/tlshandshake` | negotiate the encrypted channel |
| `build/capture` | capture one fingerprint, write PGM files |
| `build/enroll` | record a finger |
| `build/verify` | check a finger against the record |
| `build/unlock` | recover a sensor stuck mid-handshake |

```
$ ./build/verify
touch the sensor

score 0.907 against 8 templates, threshold 0.50
ACCEPTED
```

## Desktop integration

The driver plugs into libfprint, which needs two extra entry points that
mainline does not have. See [`driver/README.md`](driver/README.md) for why.

The patch is against **`v1.94.100-10-g6f9479c3`**. libfprint moves, and it
touches `fpi-image-device.c`, so check that revision out rather than master:

```sh
git clone https://gitlab.freedesktop.org/libfprint/libfprint.git
cd libfprint
git checkout 6f9479c3
git apply ../driver/libfprint-custom-matching.patch
cd ..
make sync-driver

cd libfprint
meson setup build --prefix=$HOME/.local/libfprint-dev \
      -Ddrivers=goodix5117 \
      -Dintrospection=false -Ddoc=false -Dgtk-examples=false \
      -Dinstalled-tests=false -Dudev_rules=disabled -Dudev_hwdb=disabled
ninja -C build && ninja -C build install
```

Nothing is installed over the distribution's libfprint. To point `fprintd` at
this build instead — it runs with `ProtectHome=true`, so the library has to
live outside `/home`:

```sh
sudo install -D -m 755 \
    ~/.local/libfprint-dev/lib/$(gcc -print-multiarch)/libfprint-2.so.2.0.0 \
    /usr/local/lib/goodix-libfprint/libfprint-2.so.2

sudo install -D -m 600 -o root -g root clef.txt /etc/goodix5117/psk

sudo install -D -m 644 /dev/stdin /etc/systemd/system/fprintd.service.d/goodix.conf <<'CONF'
[Service]
Environment=LD_LIBRARY_PATH=/usr/local/lib/goodix-libfprint
CONF

sudo systemctl daemon-reload && sudo systemctl restart fprintd
fprintd-enroll && fprintd-verify
```

Then `sudo pam-auth-update` and tick *Fingerprint authentication*. Ubuntu ships
`max-tries=1`, which drops to the password after a single missed touch; three
is friendlier:

```sh
sudo sed -i 's/max-tries=1 timeout=10/max-tries=3 timeout=20/' \
    /usr/share/pam-configs/fprintd
sudo pam-auth-update --package
```

That file belongs to the `libpam-fprintd` package and an upgrade will reset it.

**Keep a root shell open while configuring PAM.** A broken authentication
stack can lock you out; an open `sudo -i` is the way back.

### Removing everything

```sh
sudo rm -rf /usr/local/lib/goodix-libfprint /etc/goodix5117 \
            /etc/systemd/system/fprintd.service.d
sudo pam-auth-update --remove fprintd
sudo systemctl daemon-reload
rm -rf ~/.local/libfprint-dev
```

## How it works

### Framing

Two nested layers. The outer one is the USB envelope, the inner one carries
the command.

```
  a0 06 00 a6 | a8 03 00 00 00 ff | 00 00 ... 00
  ──                                              flags: a0 command, b0 TLS
     ─────                                        payload length, little-endian
           ──                                     sum of the three header bytes
                ──                                command
                   ─────                          length, counting the checksum
                         ─────                    payload
                               ──                 0xaa - sum
                                    └ pad to 64 ┘
```

Two length fields that differ by one, two checksums computed differently, and
padding to the endpoint's packet size. Every command answers with an ACK
echoing its own code, and only some follow it with real data.

### The encrypted channel

The image never travels in clear. What is surprising is the direction:

```
  host                                    sensor
    │   0xd0  request TLS connection  ──►   │
    │   ◄──  ClientHello                    │     the sensor is the client,
    │   ServerHello, Done  ──►              │     the host is the server
    │   ◄──  ClientKeyExchange, Finished    │
    │   ChangeCipherSpec, Finished  ──►     │
    │   0xd4  established  ──►              │
```

One cipher suite is offered and no extensions: `TLS_PSK_WITH_AES_128_GCM_SHA256`,
pure PSK, no certificate. The driver runs OpenSSL as a *server* over memory
BIOs and ferries bytes between it and USB, so nothing blocks and no socket is
involved.

A handshake left unfinished locks the sensor into a state that survives a
power cycle — command `0xd4` is the only way out. `build/unlock` sends it.

### From bytes to an image

```
  10573 bytes       8 header + payload + 5 trailer
       │
       │  unpack    6 bytes carry 4 samples of 12 bits, interleaved
       ▼
  88 x 80 samples   0..4095
       │
       │  crop      columns 64..87 always read zero — measured, not assumed
       ▼
  64 x 80
       │
       │  subtract  a background frame captured microseconds earlier
       │  equalise  local mean and variance over a 15 x 15 window
       ▼
  the fingerprint
```

The background subtraction is what makes the image legible at all: it cancels
the array's fixed noise, its bias and the vignetting at the edges, leaving
only what the finger changed.

### Matching

The frame is 64 x 80 — around an eighth of a fingertip. Running NBIS on it
finds **one** minutia where `bozorth3` wants twenty, so libfprint's default
comparison can never work here, at any threshold.

Instead the template is compared by normalised cross-correlation, sliding one
image over the other and keeping the best agreement:

```
                 mean( (a - ā)(b - b̄) )
    score  =  ───────────────────────────      1.0 identical, 0.0 unrelated
                    sd(a) × sd(b)
```

Subtracting the means and dividing by the deviations makes the score immune to
a drier or harder-pressed finger.

## Measured results

Enrolment records several touches and a probe matching any one of them is
accepted.

| | Score |
| --- | --- |
| Same finger, placed as enrolled | 0.51 – 0.91 |
| Same finger, placed carelessly | 0.34 |
| A different finger | 0.39 max |

Threshold: **0.50** for verification, **0.55** for accepting a touch during
enrolment. The two questions are not the same one — enrolment guards coherence
between captures and deserves a stricter bar, since a stray template drags
every later score down.

Placement matters more than identity on a sensor this small: a different
finger placed well scored higher than the enrolled finger placed badly. That
is the honest headline of this project.

## Limitations

**This is a convenience, not a security improvement.** The two score
populations overlap: 0.39 for a different finger against 0.51 for a correctly
placed enrolled one. Adding a fingerprint to PAM also adds a second way in,
and the strength of the whole becomes that of the weaker path. The same
applies to the vendor's own Windows implementation on this sensor.

The encrypted channel is genuinely sound — TLS 1.2, AES-128-GCM. Nobody
sniffing the USB bus can lift your fingerprint or inject a fake one.

**Enrolment scatter is a real risk, and it is measurable.** Verification takes
the best score among the enrolled templates, so more templates mean more
chances to be wrong. Templates covering scattered patches of skin push
impostor scores from 0.28 to 0.41 — past the threshold. The coherence check
during enrolment is a security measure, not a comfort feature.

**Cancellation is slow.** A blocking transfer is not interrupted, so
deactivating while the driver waits for a finger can take up to 20 seconds.

**One device at a time.** The driver assumes a single sensor.

**Tested on one machine.** An Honor MagicBook running Ubuntu 22.04 with
firmware `GF_ST411SEC_APP_12117`, against libfprint `v1.94.100-10-g6f9479c3`.
Other firmware revisions in the `GF_ST411SEC_APP_121xx` series are reported to
exist and the code does not require a specific one, but none has been tried
here.

**Known rough edges**, none of which bite today:

- `goodix_tls_take` truncates silently if a TLS record exceeds the caller's
  buffer. Handshake records run to a few hundred bytes against a 4 KB buffer.
- The driver drops a queued command if another arrives first. libfprint drives
  the states in sequence, so two never overlap.
- The worker thread and the main thread share the frame buffers without a
  lock. The state sequence keeps them apart, but by construction rather than
  by synchronisation.

## Layout

```
include/goodix/   public headers
src/              the library: device, protocol, init, session, tls, image, match
tools/            standalone programs
tests/            unit tests, no hardware needed
driver/           the libfprint driver and the patch it needs
SPEC.md           the protocol, measured
```

`make sync-driver` copies the library into a libfprint checkout. This
repository stays the only place the sources are edited.

## Credits

Everything here rests on people who spent months sniffing USB traffic and
published what they found.

- **[goodix-fp-dump](https://github.com/goodix-fp-linux-dev/goodix-fp-dump)**
  by the goodix-fp-linux-dev group — the protocol itself: command set, packet
  layout, initialisation sequence, MCU configuration blob, image encoding.
  Sponsored by @markusressel and @pjungkamp.
- **Alexander Meiler, Matthieu Charette and Natasha England-Elbro** — the
  libfprint TLS driver for the Goodix 5xx family, where the channel design
  comes from.
- **gulp** — identified the 5117 and opened the discussion that unblocked the
  key problem.
- **AntoG00** — found that the key a sensor reports is obfuscated and that the
  real one can be read out of the Windows driver's memory.
- **SenchaBrest** — found the simpler route, through the encrypted file
  Windows keeps on disk.
- **[senyakirilovich](https://gitlab.com/senyakirilovich/goodix-fingerprint-27c6-5117-psk-extraction)**
  — the tool that implements that decryption.
- **Volzhanka63** — reported an Honor MagicBook on firmware `12118`, which is
  what made a non-standard firmware version look normal rather than alarming.
- The **[libfprint](https://gitlab.freedesktop.org/libfprint/libfprint)**
  project, which this plugs into.

No code was copied from any of them. What was taken are facts about a piece of
silicon — that `0xa8` asks for the firmware version, that the checksum is
`0xaa - sum`, that pixels are interleaved. Those are discoveries, not
creations, and they are recorded in `SPEC.md` in this project's own words.

One exception, for the record: `SPEC.md` reproduces goodix-fp-dump's 96-byte
`PSK_WHITE_BOX` constant verbatim, attributed where it appears. It documents a
firmware-rewriting path this project deliberately does not implement, and no
code here uses it. goodix-fp-dump is MIT licensed.

Claude (Anthropic) was used as a pair-programming assistant throughout, for
protocol research, code review and documentation.

## Licence

LGPL-2.1-or-later, matching libfprint, which parts of this work extend.

Copyright (C) 2026 Raphaël Bély
