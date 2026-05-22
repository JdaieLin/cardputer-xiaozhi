# Store Assets

This directory contains the App Store assets referenced by `app-builder.json`.

## Files

- `screenshots/01-binding.png`
- `screenshots/02-idle.png`
- `screenshots/03-listening.png`
- `screenshots/04-speaking.png`

## Regenerate

Create or refresh the screenshots with:

```bash
/private/tmp/cardputer-xiaozhi-venv/bin/python tools/generate_store_assets.py
```

The generated screenshots are `320x170` PNG files rendered by the same `display_bridge.py`
layout used on the framebuffer path, so they stay close to the actual device UI.
