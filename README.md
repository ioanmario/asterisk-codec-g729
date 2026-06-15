# asterisk-codec-g729

Plug-and-play **G.729 transcoder** module for **Asterisk 21/22**, built on the
[bcg729](https://github.com/BelledonneCommunications/bcg729) library.

It registers the two translators Asterisk needs to transcode G.729 to/from any
PCM-based codec (PCMU/PCMA/GSM/...):

```
g729  -> slin   (decode)
slin  -> g729   (encode)
```

That's all you need. Asterisk's translation core chains `slin <-> ulaw/alaw/...`
automatically, so paths like the following are built for you:

```
g729 To ulaw : (g729@8000)->(slin@8000)->(ulaw@8000)
```

## Why a separate module?

There are three distinct pieces, often confused:

| Piece | What it is | Transcodes? |
|-------|-----------|-------------|
| **bcg729** (`libbcg729`) | Generic C library implementing the G.729 math (PCM ⇄ G.729). Knows nothing about Asterisk. | — |
| **`format_g729.so`** | Asterisk's *file/wire format* for G.729. Enables passthrough only. | No |
| **`codec_g729.so`** (this repo) | The *codec/transcoder*. Glues bcg729 into Asterisk's translation core. | **Yes** |

If `core show translation paths g729` shows **"No Translation Path"**, you have
the format but not the codec. This module is the missing piece.

`bcg729` is linked **statically** into `codec_g729.so`, so the target box does
not need a separate `libbcg729` package.

## Build & install (on the Asterisk box)

Requires: `git`, `cmake`, `gcc`, `make`, `pkg-config`, and the Asterisk
development headers (`asterisk-dev` on Debian/Ubuntu, `asterisk-devel` on RHEL).

```bash
git clone --recurse-submodules https://github.com/ioanmario/asterisk-codec-g729.git
cd asterisk-codec-g729
./install.sh
```

Then, from the Asterisk CLI:

```
module load codec_g729.so
core show translation paths g729
```

> **ABI note:** an Asterisk module must be built against headers matching the
> Asterisk version running on the box. Building on the box itself (via
> `install.sh`) is the safest path.

### Manual build

```bash
git submodule update --init --recursive
make                 # builds bcg729 (static) + codec_g729.so
sudo make install    # installs into $(pkg-config --variable=modulesdir asterisk)
```

## Debian package

```bash
sudo apt-get install build-essential cmake pkg-config asterisk-dev debhelper devscripts
dpkg-buildpackage -us -uc -b
sudo dpkg -i ../asterisk-codec-g729_1.0.0_*.deb
```

## Docker (CI / smoke build)

```bash
docker build -t codec-g729 .
docker run --rm -v "$PWD/out:/out" codec-g729 cp /build/codec_g729.so /out/
```

## Verifying it works

```
*CLI> module load codec_g729.so
*CLI> core show translation paths g729
  g729 To ulaw : (g729@8000)->(slin@8000)->(ulaw@8000)
  g729 To alaw : (g729@8000)->(slin@8000)->(alaw@8000)
```

## License

This module: **GPL-2.0-or-later** (see [LICENSE](LICENSE)). The full GPLv2 text
can be dropped in with:

```bash
curl -o LICENSE https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
```

Third-party: bcg729 © Belledonne Communications, GPL-3.0-or-later (see
[NOTICE](NOTICE)). Relevant G.729 patents have expired; verify your local
situation if in doubt.
