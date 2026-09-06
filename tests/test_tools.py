import asyncio
import base64
import importlib.util
import json
import sys
import tempfile
import unittest
from io import BytesIO
from pathlib import Path
from unittest.mock import AsyncMock, patch


TOOLS_DIR = Path(__file__).resolve().parents[1] / "main" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import mcp_tools as mcp_tools_module  # noqa: E402
from mcp_tools import McpTools  # noqa: E402

display_bridge = None
if importlib.util.find_spec("PIL") is not None:
    import display_bridge  # noqa: E402


@unittest.skipIf(display_bridge is None, "Pillow is not installed in this Python runtime")
class SubtitleTests(unittest.TestCase):
    def test_subtitle_is_always_one_line(self):
        value = display_bridge._single_line_text("first line\nsecond\tline  end")
        self.assertEqual(value, "first line second line end")
        self.assertNotIn("\n", value)

    def test_paginated_chunks_do_not_contain_newlines(self):
        lines = display_bridge._wrap_text("one\ntwo three", display_bridge._text_font, 45)
        self.assertTrue(lines)
        self.assertTrue(all("\n" not in line for line in lines))


@unittest.skipIf(display_bridge is None, "Pillow is not installed in this Python runtime")
class WatercolorResourcePressureTests(unittest.TestCase):
    def setUp(self):
        names = (
            "_watercolor", "_watercolor_selected", "_watercolor_suspended",
            "_command_active", "_resource_last_check", "_resource_recovery_since",
            "_last_frame_key", "_last_static_frame_key", "SNAPSHOT_PATH",
        )
        self.saved = {name: getattr(display_bridge, name) for name in names}

    def tearDown(self):
        for name, value in self.saved.items():
            setattr(display_bridge, name, value)

    def test_low_memory_suspends_and_recovers_after_delay(self):
        renderer = object()
        display_bridge._watercolor = renderer
        display_bridge._watercolor_selected = True
        display_bridge._watercolor_suspended = False
        display_bridge._command_active = True
        display_bridge._resource_last_check = 0.0
        display_bridge._resource_recovery_since = None

        # Below the command threshold but above the emergency threshold.
        low = {"MemAvailable": 48 * 1024, "CmaFree": 2 * 1024}
        with patch.object(display_bridge, "_read_memory_info_kb", return_value=low):
            display_bridge._update_resource_pressure(1.0)
        self.assertTrue(display_bridge._watercolor_suspended)
        self.assertIsNone(display_bridge._watercolor)

        display_bridge._command_active = False
        recovered = {"MemAvailable": 160 * 1024, "CmaFree": 20 * 1024}
        with patch.object(display_bridge, "_read_memory_info_kb", return_value=recovered), \
             patch.object(display_bridge, "_load_watercolor_renderer", return_value=renderer):
            display_bridge._update_resource_pressure(2.0)
            self.assertIsNone(display_bridge._watercolor)
            display_bridge._update_resource_pressure(
                2.0 + display_bridge.WATERCOLOR_RECOVERY_DELAY_SEC + 0.6
            )
        self.assertIs(display_bridge._watercolor, renderer)
        self.assertFalse(display_bridge._watercolor_suspended)

    def test_critical_memory_suspends_even_without_command_event(self):
        renderer = object()
        display_bridge._watercolor = renderer
        display_bridge._watercolor_selected = True
        display_bridge._watercolor_suspended = False
        display_bridge._command_active = False
        display_bridge._resource_last_check = 0.0
        critical = {"MemAvailable": 20 * 1024, "CmaFree": 20 * 1024}
        with patch.object(display_bridge, "_read_memory_info_kb", return_value=critical):
            display_bridge._update_resource_pressure(1.0)
        self.assertIsNone(display_bridge._watercolor)
        self.assertTrue(display_bridge._watercolor_suspended)

    def test_normal_memory_does_not_suspend(self):
        renderer = object()
        display_bridge._watercolor = renderer
        display_bridge._watercolor_selected = True
        display_bridge._watercolor_suspended = False
        display_bridge._command_active = True
        display_bridge._resource_last_check = 0.0
        healthy = {"MemAvailable": 160 * 1024, "CmaFree": 20 * 1024}
        with patch.object(display_bridge, "_read_memory_info_kb", return_value=healthy):
            display_bridge._update_resource_pressure(1.0)
        self.assertIs(display_bridge._watercolor, renderer)
        self.assertFalse(display_bridge._watercolor_suspended)

    def test_suspension_warning_is_drawn_on_left(self):
        display_bridge._watercolor = None
        display_bridge._watercolor_selected = True
        display_bridge._watercolor_suspended = True
        display_bridge._last_frame_key = None
        display_bridge._last_static_frame_key = None
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory) / "warning.png"
            display_bridge.SNAPSHOT_PATH = str(snapshot)
            display_bridge.render_frame("THINKING", "🤔", "", "", "$ test")
            image = display_bridge.Image.open(snapshot).convert("RGB")
            warning_area = image.crop((0, 104, 230, 132))
            self.assertTrue(any(
                red > 200 and 100 < green < 235 and blue < 130
                for red, green, blue in warning_area.getdata()
            ))


@unittest.skipIf(display_bridge is None, "Pillow is not installed in this Python runtime")
class CameraPreviewRenderTests(unittest.TestCase):
    def setUp(self):
        names = (
            "_camera_frame", "_camera_frame_version", "_watercolor",
            "_last_frame_key", "_last_static_frame_key", "SNAPSHOT_PATH",
        )
        self.saved = {name: getattr(display_bridge, name) for name in names}

    def tearDown(self):
        current = display_bridge._camera_frame
        if current is not None and current is not self.saved["_camera_frame"]:
            current.close()
        for name, value in self.saved.items():
            setattr(display_bridge, name, value)

    def test_camera_photo_fills_left_area_between_header_and_footer(self):
        source = display_bridge.Image.new("RGB", (640, 480), (230, 25, 20))
        encoded = BytesIO()
        source.save(encoded, format="JPEG", quality=90)
        source.close()

        display_bridge._watercolor = None
        display_bridge._set_camera_frame(base64.b64encode(encoded.getvalue()).decode("ascii"))
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory) / "camera-preview.png"
            display_bridge.SNAPSHOT_PATH = str(snapshot)
            display_bridge.render_frame("THINKING", "🤔", "", "", "camera\nAnalyzing...")
            image = display_bridge.Image.open(snapshot).convert("RGB")
            red, green, blue = image.getpixel((20, 60))
            self.assertGreater(red, 180)
            self.assertLess(green, 70)
            self.assertLess(blue, 70)
            self.assertNotEqual(image.getpixel((20, 20)), image.getpixel((20, 60)))
            self.assertNotEqual(image.getpixel((20, 150)), image.getpixel((20, 60)))
            image.close()

        display_bridge._set_camera_frame("")
        self.assertIsNone(display_bridge._camera_frame)


@unittest.skipIf(display_bridge is None, "Pillow is not installed in this Python runtime")
class CameraCompressionTests(unittest.TestCase):
    def test_camera_upload_is_resized_and_bounded(self):
        source = display_bridge.Image.new("RGB", (1200, 900), (80, 140, 210))
        original = BytesIO()
        source.save(original, format="JPEG", quality=95)
        source.close()

        compressed = McpTools._prepare_camera_upload_jpeg(original.getvalue())
        self.assertLessEqual(len(compressed), mcp_tools_module.CAMERA_UPLOAD_MAX_BYTES)
        with display_bridge.Image.open(BytesIO(compressed)) as image:
            self.assertLessEqual(image.width, mcp_tools_module.CAMERA_UPLOAD_MAX_WIDTH)
            self.assertLessEqual(image.height, mcp_tools_module.CAMERA_UPLOAD_MAX_HEIGHT)

    def test_camera_upload_is_rotated_for_device_mounting(self):
        source = display_bridge.Image.new("RGB", (400, 300), (225, 30, 20))
        draw = display_bridge.ImageDraw.Draw(source)
        draw.rectangle((0, 150, 400, 300), fill=(20, 40, 225))
        original = BytesIO()
        source.save(original, format="JPEG", quality=95)
        source.close()

        compressed = McpTools._prepare_camera_upload_jpeg(original.getvalue())
        with display_bridge.Image.open(BytesIO(compressed)).convert("RGB") as image:
            top = image.getpixel((image.width // 2, 30))
            bottom = image.getpixel((image.width // 2, image.height - 30))
            self.assertGreater(top[2], top[0])
            self.assertGreater(bottom[0], bottom[2])


class CameraToolTests(unittest.IsolatedAsyncioTestCase):
    async def test_camera_is_listed_and_uses_vision_capability(self):
        previews = []
        tools = McpTools(
            device_id="device",
            client_id="client",
            camera_available=True,
            camera_preview=previews.append,
        )
        init = await tools.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "vision": {"url": "https://vision.example.test/explain", "token": "secret"}
                },
            },
        })
        self.assertEqual(init["result"]["protocolVersion"], "2024-11-05")
        self.assertEqual(tools.vision_url, "https://vision.example.test/explain")

        listed = await tools.handle({
            "jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}
        })
        names = {item["name"] for item in listed["result"]["tools"]}
        self.assertIn("self.camera.take_photo", names)

        captured = b"\xff\xd8photo\xff\xd9"
        compressed = b"\xff\xd8small\xff\xd9"
        tools._capture_camera_jpeg = AsyncMock(return_value=captured)
        with patch.object(tools, "_prepare_camera_upload_jpeg", return_value=compressed), \
             patch.object(tools, "_explain_camera_jpeg", return_value={"answer": "a desk"}) as explain:
            called = await tools.handle({
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "self.camera.take_photo",
                    "arguments": {"question": "What is here?"},
                },
            })
        content = called["result"]["content"][0]
        self.assertEqual(content["type"], "text")
        self.assertEqual(json.loads(content["text"]), {"answer": "a desk"})
        self.assertEqual(previews, [compressed])
        explain.assert_called_once_with(compressed, "What is here?")

    async def test_camera_rejects_missing_vision_capability(self):
        tools = McpTools(camera_available=True)
        response = await tools.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "self.camera.take_photo",
                "arguments": {"question": "What is here?"},
            },
        })
        self.assertIn("vision capability is unavailable", response["result"]["error"])


class CommandActivityTests(unittest.IsolatedAsyncioTestCase):
    async def test_local_command_has_explicit_activity_lifecycle(self):
        activity = []
        tools = McpTools(command_activity=activity.append)
        description, schema, _ = tools.tools["local_command"]
        handler = AsyncMock(return_value={"status": "completed", "exit_code": 0})
        tools.tools["local_command"] = (description, schema, handler)
        await tools.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "local_command", "arguments": {"command": "date"}},
        })
        self.assertEqual(activity, [True, False])

    async def test_activity_stays_on_until_background_process_exits(self):
        activity = []
        tools = McpTools(command_activity=activity.append)
        with patch.object(mcp_tools_module, "LOCAL_COMMAND_ALLOW_DANGEROUS", True):
            response = await tools.handle({
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {
                    "name": "local_command",
                    "arguments": {"command": "sh -c 'sleep 0.4'", "timeout": 0.1},
                },
            })
        value = json.loads(response["result"]["content"][0]["text"])
        self.assertEqual(value["status"], "running")
        self.assertEqual(activity, [True])
        await asyncio.sleep(0.5)
        self.assertEqual(activity, [True, False])


if __name__ == "__main__":
    unittest.main()
