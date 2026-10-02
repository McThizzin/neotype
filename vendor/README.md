# vendor

Third-party single-header libraries. Nothing here is edited.

## miniaudio.h

| | |
|---|---|
| Upstream | https://github.com/mackron/miniaudio |
| Pinned tag | `0.11.25` |
| SHA-256 | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` |
| Used by | `src/audio.c`, device playback only |

`src/audio.c` compiles with `MA_NO_DECODING`, `MA_NO_ENCODING`, `MA_NO_GENERATION`,
`MA_NO_RESOURCE_MANAGER`, `MA_NO_NODE_GRAPH` and `MA_NO_ENGINE` -- the game only
ever needs to hand a float buffer to an output device. The mixer itself is
device-free, so the audio tests link the same file without opening a sound card.

To move the pin, download the new tag and re-check both the version macros
(`MA_VERSION_MAJOR/MINOR/REVISION`) and the hash:

```
curl -sSfL -o vendor/miniaudio.h \
    https://raw.githubusercontent.com/mackron/miniaudio/<tag>/miniaudio.h
sha256sum vendor/miniaudio.h
```

The file is included as `"vendor/miniaudio.h"` (with `-I.` in the Makefile) on
purpose: `tests/check-layers.sh` treats a bare lowercase `.h` name as a local
module and anything path-qualified as external.
