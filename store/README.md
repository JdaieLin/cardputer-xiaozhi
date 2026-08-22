# Store Assets

This directory contains the App Store assets referenced by `app-builder.json`.

## Files

- `screenshots/01-binding.png`
- `screenshots/02-idle.png`
- `screenshots/03-listening.png`
- `screenshots/04-speaking.png`

## Regenerate

The checked-in screenshots are generated on a CardputerZero device with the
installed framebuffer renderer and then copied back into this directory. This
ensures the store preview uses the same fonts, downloaded Whisplay emoji assets,
and rendering path as the physical display.

For local layout development only, approximate screenshots can be generated with:

```bash
/private/tmp/cardputer-xiaozhi-venv/bin/python tools/generate_store_assets.py
```

These local images are not a replacement for the final `320x170` device-generated
store assets.
