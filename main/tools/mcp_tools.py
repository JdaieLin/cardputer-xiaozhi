#!/usr/bin/env python3
"""Small, dependency-free MCP tool registry used by the WebSocket bridge."""

import asyncio
import base64
import html
import json
import os
import re
import shlex
import shutil
import time
import uuid
from dataclasses import dataclass, field
from html.parser import HTMLParser
from io import BytesIO
from typing import Any, Awaitable, Callable
from urllib.parse import parse_qs, quote_plus, unquote, urlparse
from urllib.request import ProxyHandler, Request, build_opener
from xml.etree import ElementTree


ProgressCallback = Callable[[str | None], None]
ActivityCallback = Callable[[bool], None]
CameraPreviewCallback = Callable[[bytes | None], None]
ToolHandler = Callable[[dict[str, Any], ProgressCallback | None], Awaitable[dict[str, Any]]]


def _env_bool(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_float(name: str, default: float) -> float:
    try:
        return float(os.getenv(name, str(default)))
    except ValueError:
        return default


def _env_int(name: str, default: int) -> int:
    try:
        return int(os.getenv(name, str(default)))
    except ValueError:
        return default


LOCAL_COMMAND_ENABLED = _env_bool("XIAOZHI_LOCAL_COMMAND_TOOL_ENABLED", True)
LOCAL_COMMAND_ALLOW_DANGEROUS = _env_bool(
    "XIAOZHI_LOCAL_COMMAND_ALLOW_DANGEROUS",
    _env_bool("XIAOZHI_LOCAL_COMMAND_UNSAFE", False),
)
LOCAL_COMMAND_USE_SHELL = _env_bool("XIAOZHI_LOCAL_COMMAND_USE_SHELL", False)
LOCAL_COMMAND_TIMEOUT = max(0.1, _env_float("XIAOZHI_LOCAL_COMMAND_TIMEOUT_SEC", 5.0))
LOCAL_COMMAND_OUTPUT_LIMIT = max(256, _env_int("XIAOZHI_LOCAL_COMMAND_OUTPUT_LIMIT", 4000))
SUDO_PASSWORD_PLACEHOLDER = "{{SUDO_PASSWORD}}"
LOCAL_COMMAND_SUDO_PASSWORD = os.environ.pop("XIAOZHI_LOCAL_COMMAND_SUDO_PASSWORD", "")
LOCAL_COMMAND_ALLOWLIST = {
    item.strip()
    for item in os.getenv(
        "XIAOZHI_LOCAL_COMMAND_ALLOWLIST",
        "date,uptime,hostname,whoami,df,free,ip,iwgetid,vcgencmd",
    ).split(",")
    if item.strip()
}

WEB_SEARCH_ENABLED = _env_bool("XIAOZHI_WEB_TOOLS_ENABLED", True)
WEB_TIMEOUT = max(1.0, _env_float("XIAOZHI_WEB_TOOL_TIMEOUT_SEC", 15.0))
WEB_RESULT_LIMIT = max(1, _env_int("XIAOZHI_WEB_SEARCH_RESULT_LIMIT", 5))

CAMERA_ENABLED = _env_bool("XIAOZHI_CAMERA_TOOL_ENABLED", True)
CAMERA_DEVICE = os.getenv("XIAOZHI_CAMERA_DEVICE", "/dev/video0").strip()
CAMERA_COMMAND = os.getenv("XIAOZHI_CAMERA_COMMAND", "rpicam-still").strip()
CAMERA_WIDTH = max(160, min(1920, _env_int("XIAOZHI_CAMERA_WIDTH", 640)))
CAMERA_HEIGHT = max(120, min(1080, _env_int("XIAOZHI_CAMERA_HEIGHT", 480)))
CAMERA_QUALITY = max(30, min(95, _env_int("XIAOZHI_CAMERA_JPEG_QUALITY", 80)))
CAMERA_WARMUP_MS = max(100, min(5000, _env_int("XIAOZHI_CAMERA_WARMUP_MS", 600)))
CAMERA_SENSOR_MODE = os.getenv("XIAOZHI_CAMERA_SENSOR_MODE", "640:480:10:P").strip()
CAMERA_TIMEOUT = max(2.0, _env_float("XIAOZHI_CAMERA_TIMEOUT_SEC", 12.0))
CAMERA_VISION_TIMEOUT = max(3.0, _env_float("XIAOZHI_CAMERA_VISION_TIMEOUT_SEC", 30.0))
CAMERA_MAX_JPEG_BYTES = max(64 * 1024, _env_int("XIAOZHI_CAMERA_MAX_JPEG_BYTES", 4 * 1024 * 1024))
CAMERA_UPLOAD_MAX_WIDTH = max(160, min(1280, _env_int("XIAOZHI_CAMERA_UPLOAD_MAX_WIDTH", 512)))
CAMERA_UPLOAD_MAX_HEIGHT = max(120, min(960, _env_int("XIAOZHI_CAMERA_UPLOAD_MAX_HEIGHT", 384)))
CAMERA_UPLOAD_QUALITY = max(40, min(90, _env_int("XIAOZHI_CAMERA_UPLOAD_JPEG_QUALITY", 72)))
_camera_rotation = _env_int("XIAOZHI_CAMERA_ROTATION", 180)
CAMERA_ROTATION = _camera_rotation if _camera_rotation in {0, 90, 180, 270} else 180
CAMERA_UPLOAD_MAX_BYTES = max(
    64 * 1024,
    min(2 * 1024 * 1024, _env_int("XIAOZHI_CAMERA_UPLOAD_MAX_BYTES", 256 * 1024)),
)
CAMERA_MAX_RESPONSE_BYTES = max(16 * 1024, _env_int("XIAOZHI_CAMERA_MAX_RESPONSE_BYTES", 512 * 1024))


LOCAL_COMMAND_SCHEMA = {
    "type": "object",
    "properties": {
        "command": {"type": "string", "description": "Command and arguments to run on this device."},
        "timeout": {"type": "number", "description": "Foreground wait timeout in seconds."},
    },
    "required": ["command"],
}
JOB_SCHEMA = {
    "type": "object",
    "properties": {"job_id": {"type": "string", "description": "Background command job id."}},
    "required": ["job_id"],
}
WEB_SEARCH_SCHEMA = {
    "type": "object",
    "properties": {
        "query": {"type": "string", "description": "Search query."},
        "num_results": {"type": "number", "description": "Maximum number of results."},
        "search_type": {"type": "string", "description": "web or news."},
    },
    "required": ["query"],
}
CAMERA_SCHEMA = {
    "type": "object",
    "properties": {
        "question": {
            "type": "string",
            "description": "The question to answer about the newly captured photo.",
        },
    },
    "required": ["question"],
}


def _clip(text: str, limit: int = LOCAL_COMMAND_OUTPUT_LIMIT) -> str:
    if len(text) <= limit:
        return text
    return text[:limit].rstrip() + f"\n...[truncated {len(text) - limit} chars]"


def _redact_secret(text: str) -> str:
    if LOCAL_COMMAND_SUDO_PASSWORD:
        return text.replace(LOCAL_COMMAND_SUDO_PASSWORD, SUDO_PASSWORD_PLACEHOLDER)
    return text


def _tail(stdout: bytes, stderr: bytes, command: str, line_limit: int = 5) -> str:
    output = _redact_secret("\n".join(
        part.decode("utf-8", "replace").rstrip("\n")
        for part in (stdout, stderr)
        if part
    ))
    lines = [line for line in output.replace("\r", "").split("\n") if line]
    return "\n".join(lines[-line_limit:]) if lines else f"$ {command}"


@dataclass
class CommandJob:
    job_id: str
    command: str
    process: asyncio.subprocess.Process
    progress: ProgressCallback | None = None
    started_at: float = field(default_factory=time.time)
    stdout: bytearray = field(default_factory=bytearray)
    stderr: bytearray = field(default_factory=bytearray)
    readers: list[asyncio.Task] = field(default_factory=list)
    monitor: asyncio.Task | None = None
    status: str = "running"
    exit_code: int | None = None


_JOBS: dict[str, CommandJob] = {}


async def _read_stream(stream, target: bytearray, job: CommandJob) -> None:
    if stream is None:
        return
    while True:
        chunk = await stream.read(512)
        if not chunk:
            return
        target.extend(chunk)
        if job.progress:
            job.progress(_tail(bytes(job.stdout), bytes(job.stderr), job.command))


def _job_result(job: CommandJob) -> dict[str, Any]:
    result: dict[str, Any] = {
        "command": job.command,
        "status": job.status,
        "job_id": job.job_id,
        "running_seconds": round(time.time() - job.started_at, 1),
        "output_tail": _tail(bytes(job.stdout), bytes(job.stderr), job.command),
        "stdout": _clip(_redact_secret(bytes(job.stdout).decode("utf-8", "replace"))),
        "stderr": _clip(_redact_secret(bytes(job.stderr).decode("utf-8", "replace"))),
    }
    if job.exit_code is not None:
        result["exit_code"] = job.exit_code
    return result


async def _monitor(job: CommandJob) -> None:
    await job.process.wait()
    await asyncio.gather(*job.readers, return_exceptions=True)
    if job.status == "running":
        job.status = "completed"
    job.exit_code = job.process.returncode
    if job.progress:
        job.progress(f"{job.status} exit={job.exit_code}\n{_tail(bytes(job.stdout), bytes(job.stderr), job.command)}")


async def local_command(params: dict[str, Any], progress: ProgressCallback | None = None) -> dict[str, Any]:
    command = str(params.get("command", "")).strip()
    if not command:
        raise ValueError("command is required")
    try:
        argv = shlex.split(command)
    except ValueError as exc:
        raise ValueError(f"invalid command: {exc}") from exc
    if not argv:
        raise ValueError("command is required")
    executable = os.path.basename(argv[0])
    placeholder_tokens = [arg for arg in argv if SUDO_PASSWORD_PLACEHOLDER in arg]
    uses_sudo_password = bool(placeholder_tokens)
    if uses_sudo_password:
        if placeholder_tokens != [SUDO_PASSWORD_PLACEHOLDER]:
            raise ValueError(f"{SUDO_PASSWORD_PLACEHOLDER} must be a standalone command argument")
        if not LOCAL_COMMAND_ALLOW_DANGEROUS:
            raise PermissionError("sudo password placeholder requires XIAOZHI_LOCAL_COMMAND_ALLOW_DANGEROUS=true")
        if not LOCAL_COMMAND_SUDO_PASSWORD:
            raise PermissionError("sudo password is not configured")
        if "\n" in LOCAL_COMMAND_SUDO_PASSWORD or "\r" in LOCAL_COMMAND_SUDO_PASSWORD:
            raise ValueError("configured sudo password must not contain a newline")
        if executable != "sudo":
            raise PermissionError(f"{SUDO_PASSWORD_PLACEHOLDER} may only be used with sudo")
        argv = [arg for arg in argv if arg != SUDO_PASSWORD_PLACEHOLDER]
        if "-S" not in argv[1:]:
            argv.insert(1, "-S")
        if "-p" not in argv[1:]:
            argv[1:1] = ["-p", ""]
    if not LOCAL_COMMAND_ALLOW_DANGEROUS and executable not in LOCAL_COMMAND_ALLOWLIST:
        allowed = ", ".join(sorted(LOCAL_COMMAND_ALLOWLIST)) or "(none)"
        raise PermissionError(f"command '{executable}' is not allowlisted. Allowed: {allowed}")
    timeout = min(LOCAL_COMMAND_TIMEOUT, max(0.1, float(params.get("timeout", LOCAL_COMMAND_TIMEOUT))))
    if LOCAL_COMMAND_USE_SHELL and not uses_sudo_password:
        if not LOCAL_COMMAND_ALLOW_DANGEROUS:
            raise PermissionError("shell execution requires XIAOZHI_LOCAL_COMMAND_ALLOW_DANGEROUS=true")
        process = await asyncio.create_subprocess_shell(
            command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE
        )
    else:
        process = await asyncio.create_subprocess_exec(
            *argv,
            stdin=asyncio.subprocess.PIPE if uses_sudo_password else None,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
    if uses_sudo_password and process.stdin is not None:
        process.stdin.write((LOCAL_COMMAND_SUDO_PASSWORD + "\n").encode("utf-8"))
        await process.stdin.drain()
        process.stdin.close()
    if progress:
        progress(f"$ {command}")
    job = CommandJob(uuid.uuid4().hex[:8], command, process, progress)
    job.readers = [
        asyncio.create_task(_read_stream(process.stdout, job.stdout, job)),
        asyncio.create_task(_read_stream(process.stderr, job.stderr, job)),
    ]
    try:
        await asyncio.wait_for(process.wait(), timeout=timeout)
        await asyncio.gather(*job.readers)
    except asyncio.TimeoutError:
        _JOBS[job.job_id] = job
        job.monitor = asyncio.create_task(_monitor(job))
        if progress:
            progress(f"running job {job.job_id}\n{_tail(bytes(job.stdout), bytes(job.stderr), command)}")
        return _job_result(job)
    job.status = "completed"
    job.exit_code = process.returncode
    if progress:
        progress(f"completed exit={job.exit_code}\n{_tail(bytes(job.stdout), bytes(job.stderr), command)}")
    return _job_result(job)


async def check_command(params: dict[str, Any], progress: ProgressCallback | None = None) -> dict[str, Any]:
    job_id = str(params.get("job_id", "")).strip()
    job = _JOBS.get(job_id)
    if not job:
        raise ValueError(f"unknown job_id: {job_id}")
    result = _job_result(job)
    if progress:
        progress(f"{job.status} {job.job_id}\n{result['output_tail']}")
    if job.status != "running":
        _JOBS.pop(job_id, None)
    return result


async def stop_command(params: dict[str, Any], progress: ProgressCallback | None = None) -> dict[str, Any]:
    job_id = str(params.get("job_id", "")).strip()
    job = _JOBS.get(job_id)
    if not job:
        raise ValueError(f"unknown job_id: {job_id}")
    if job.status == "running":
        job.status = "stopped"
        job.process.terminate()
        try:
            await asyncio.wait_for(job.process.wait(), timeout=2.0)
        except asyncio.TimeoutError:
            job.process.kill()
            await job.process.wait()
        await asyncio.gather(*job.readers, return_exceptions=True)
        job.exit_code = job.process.returncode
    result = _job_result(job)
    _JOBS.pop(job_id, None)
    if progress:
        progress(f"stopped {job.job_id}\n{result['output_tail']}")
    return result


class DuckDuckGoParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.results: list[dict[str, str]] = []
        self.url = ""
        self.text: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        attrs_map = dict(attrs)
        if tag == "a" and "result__a" in attrs_map.get("class", "").split():
            href = attrs_map.get("href", "")
            if href.startswith("//"):
                href = "https:" + href
            parsed = urlparse(href)
            if parsed.netloc.endswith("duckduckgo.com"):
                href = unquote(parse_qs(parsed.query).get("uddg", [""])[0])
            self.url = href if urlparse(href).scheme in {"http", "https"} else ""
            self.text = []

    def handle_data(self, data: str) -> None:
        if self.url:
            self.text.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag == "a" and self.url:
            title = re.sub(r"\s+", " ", html.unescape(" ".join(self.text))).strip()
            if title:
                self.results.append({"title": title, "url": self.url})
            self.url = ""
            self.text = []


class BingParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.results: list[dict[str, str]] = []
        self.in_result = 0
        self.in_heading = False
        self.url = ""
        self.text: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        attrs_map = dict(attrs)
        if tag == "li" and "b_algo" in attrs_map.get("class", "").split():
            self.in_result = 1
            return
        if self.in_result:
            if tag == "li":
                self.in_result += 1
            if tag == "h2":
                self.in_heading = True
            if tag == "a" and self.in_heading and not self.url:
                href = attrs_map.get("href", "")
                if urlparse(href).scheme in {"http", "https"}:
                    self.url = _extract_bing_url(href)
                    self.text = []

    def handle_data(self, data: str) -> None:
        if self.in_result and self.url:
            self.text.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag == "a" and self.in_result and self.url:
            title = re.sub(r"\s+", " ", html.unescape(" ".join(self.text))).strip()
            if title:
                self.results.append({"title": title, "url": self.url})
            self.url = ""
            self.text = []
        if tag == "h2":
            self.in_heading = False
        if tag == "li" and self.in_result:
            self.in_result -= 1


def _extract_bing_url(url: str) -> str:
    parsed = urlparse(url)
    if not parsed.netloc.endswith("bing.com"):
        return url
    encoded = parse_qs(parsed.query).get("u", [""])[0]
    if not encoded.startswith("a1"):
        return url
    payload = encoded[2:]
    try:
        payload += "=" * (-len(payload) % 4)
        decoded = base64.urlsafe_b64decode(payload).decode("utf-8")
        if urlparse(decoded).scheme in {"http", "https"}:
            return decoded
    except Exception:
        pass
    return url


def _web_open(url: str) -> tuple[str, str]:
    proxy = _proxy_value()
    opener = build_opener(ProxyHandler({"http": proxy, "https": proxy}) if proxy else ProxyHandler())
    request = Request(
        url,
        headers={
            "User-Agent": "Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 Chrome/125 Safari/537.36",
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.7",
        },
    )
    with opener.open(request, timeout=WEB_TIMEOUT) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        return response.geturl(), response.read(2 * 1024 * 1024).decode(charset, "replace")


def _proxy_value() -> str:
    return (
        os.getenv("XIAOZHI_WEB_TOOL_PROXY", "").strip()
        or os.getenv("HTTPS_PROXY", "").strip()
        or os.getenv("HTTP_PROXY", "").strip()
    )


def _search_web(query: str, limit: int) -> dict[str, Any]:
    errors = []
    bing = ("bing_html", f"https://www.bing.com/search?q={quote_plus(query)}", BingParser)
    duckduckgo = (
        "duckduckgo_html",
        f"https://html.duckduckgo.com/html/?q={quote_plus(query)}",
        DuckDuckGoParser,
    )
    providers = (duckduckgo, bing) if _proxy_value() else (bing, duckduckgo)
    for source, requested, parser_type in providers:
        try:
            final_url, body = _web_open(requested)
            parser = parser_type()
            parser.feed(body)
            seen: set[str] = set()
            results = []
            for result in parser.results:
                if result["url"] in seen:
                    continue
                seen.add(result["url"])
                results.append(result)
                if len(results) >= limit:
                    break
            if results:
                return {"query": query, "url": final_url, "source": source, "results": results}
            errors.append(f"{source}: no results")
        except Exception as exc:
            errors.append(f"{source}: {exc}")
    raise RuntimeError("; ".join(errors))


def _search_news(query: str, limit: int) -> dict[str, Any]:
    requested = (
        "https://news.google.com/rss/search?"
        f"q={quote_plus(query)}&hl=zh-CN&gl=CN&ceid=CN:zh-Hans"
    )
    final_url, body = _web_open(requested)
    root = ElementTree.fromstring(body)
    results = []
    for item in root.findall(".//item"):
        title = re.sub(r"\s+", " ", html.unescape(item.findtext("title") or "")).strip()
        url = (item.findtext("link") or "").strip()
        if title and url:
            results.append({"title": title, "url": url})
        if len(results) >= limit:
            break
    return {"query": query, "url": final_url, "source": "google_news_rss", "results": results}


async def web_search(params: dict[str, Any], progress: ProgressCallback | None = None) -> dict[str, Any]:
    query = str(params.get("query", "")).strip()
    if not query:
        raise ValueError("query is required")
    limit = max(1, min(int(params.get("num_results", WEB_RESULT_LIMIT)), WEB_RESULT_LIMIT))
    search_type = str(params.get("search_type", "web")).strip().lower()
    if progress:
        progress(f"web_search {search_type}\n{query[:100]}")
    if search_type in {"news", "google_news"}:
        try:
            result = await asyncio.to_thread(_search_news, query, limit)
        except Exception:
            result = await asyncio.to_thread(_search_web, query, limit)
    else:
        result = await asyncio.to_thread(_search_web, query, limit)
    if progress:
        results = result.get("results", [])
        first = results[0].get("title", "no results") if results else "no results"
        progress(f"{result['source']}\n{len(results)} results\n{first[:100]}")
    return result


class McpTools:
    def __init__(
        self,
        progress: ProgressCallback | None = None,
        command_activity: ActivityCallback | None = None,
        camera_preview: CameraPreviewCallback | None = None,
        device_id: str = "",
        client_id: str = "",
        camera_available: bool | None = None,
    ) -> None:
        self.progress = progress
        self.command_activity = command_activity
        self.camera_preview = camera_preview
        self.device_id = device_id
        self.client_id = client_id
        self.vision_url = ""
        self.vision_token = ""
        self.tools: dict[str, tuple[str, dict[str, Any], ToolHandler]] = {}
        if LOCAL_COMMAND_ENABLED:
            self.register(
                "local_command",
                self._local_command_description(),
                LOCAL_COMMAND_SCHEMA,
                local_command,
            )
            self.register("checkCommand", "Check a background local command job.", JOB_SCHEMA, check_command)
            self.register("stopCommand", "Stop a background local command job.", JOB_SCHEMA, stop_command)
        if WEB_SEARCH_ENABLED:
            self.register(
                "web_search",
                "Search the web and return compact result titles and URLs. Use search_type=news for news.",
                WEB_SEARCH_SCHEMA,
                web_search,
            )
        if camera_available is None:
            camera_available = bool(
                CAMERA_ENABLED
                and CAMERA_DEVICE
                and os.path.exists(CAMERA_DEVICE)
                and CAMERA_COMMAND
                and shutil.which(CAMERA_COMMAND)
            )
        if camera_available:
            self.register(
                "self.camera.take_photo",
                "Always remember you have a camera. If the user asks you to see something, "
                "use this tool to take a photo and answer the given question about it.",
                CAMERA_SCHEMA,
                self._camera_take_photo,
            )

    def _configure_capabilities(self, params: dict[str, Any]) -> None:
        capabilities = params.get("capabilities") or {}
        vision = capabilities.get("vision") if isinstance(capabilities, dict) else None
        if not isinstance(vision, dict):
            return
        url = str(vision.get("url", "")).strip()
        if url and urlparse(url).scheme in {"http", "https"}:
            self.vision_url = url
            self.vision_token = str(vision.get("token", "")).strip()

    async def _capture_camera_jpeg(self) -> bytes:
        args = [
            CAMERA_COMMAND,
            "--nopreview",
            "--timeout", str(CAMERA_WARMUP_MS),
            "--no-raw",
            "--viewfinder-width", str(CAMERA_WIDTH),
            "--viewfinder-height", str(CAMERA_HEIGHT),
            "--viewfinder-mode", CAMERA_SENSOR_MODE,
            "--mode", CAMERA_SENSOR_MODE,
            "--buffer-count", "1",
            "--viewfinder-buffer-count", "1",
            "--width", str(CAMERA_WIDTH),
            "--height", str(CAMERA_HEIGHT),
            "--quality", str(CAMERA_QUALITY),
            "--thumb", "none",
            "--denoise", "off",
            "--output", "-",
        ]
        process = await asyncio.create_subprocess_exec(
            *args,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=CAMERA_TIMEOUT)
        except asyncio.TimeoutError:
            process.kill()
            await process.wait()
            raise RuntimeError("camera capture timed out")
        if process.returncode != 0:
            detail = stderr.decode("utf-8", "replace").strip().splitlines()
            raise RuntimeError(detail[-1] if detail else f"camera capture failed ({process.returncode})")
        if len(stdout) > CAMERA_MAX_JPEG_BYTES:
            raise RuntimeError(f"camera JPEG is too large ({len(stdout)} bytes)")
        if len(stdout) < 4 or not stdout.startswith(b"\xff\xd8") or not stdout.rstrip().endswith(b"\xff\xd9"):
            raise RuntimeError("camera did not return a valid JPEG")
        return stdout

    @staticmethod
    def _prepare_camera_upload_jpeg(jpeg: bytes) -> bytes:
        """Resize and recompress a capture before previewing and uploading it."""
        try:
            from PIL import Image, ImageOps
        except ImportError as exc:
            raise RuntimeError("camera image compression requires Pillow") from exc

        with Image.open(BytesIO(jpeg)) as source:
            source.load()
            image = ImageOps.exif_transpose(source).convert("RGB")

        try:
            transpose = {
                90: Image.Transpose.ROTATE_90,
                180: Image.Transpose.ROTATE_180,
                270: Image.Transpose.ROTATE_270,
            }.get(CAMERA_ROTATION)
            if transpose is not None:
                rotated = image.transpose(transpose)
                image.close()
                image = rotated
            image.thumbnail(
                (CAMERA_UPLOAD_MAX_WIDTH, CAMERA_UPLOAD_MAX_HEIGHT),
                Image.Resampling.LANCZOS,
            )
            quality = CAMERA_UPLOAD_QUALITY
            encoded = b""
            while quality >= 40:
                output = BytesIO()
                image.save(
                    output,
                    format="JPEG",
                    quality=quality,
                    subsampling="4:2:0",
                    optimize=False,
                )
                encoded = output.getvalue()
                if len(encoded) <= CAMERA_UPLOAD_MAX_BYTES:
                    return encoded
                quality -= 8
            raise RuntimeError(
                f"compressed camera JPEG is too large ({len(encoded)} bytes)"
            )
        finally:
            image.close()

    def _explain_camera_jpeg(self, jpeg: bytes, question: str) -> Any:
        boundary = f"----XIAOZHI_CAMERA_{uuid.uuid4().hex}"
        body = bytearray()
        body.extend(f"--{boundary}\r\n".encode())
        body.extend(b'Content-Disposition: form-data; name="question"\r\n\r\n')
        body.extend(question.encode("utf-8"))
        body.extend(b"\r\n")
        body.extend(f"--{boundary}\r\n".encode())
        body.extend(b'Content-Disposition: form-data; name="file"; filename="camera.jpg"\r\n')
        body.extend(b"Content-Type: image/jpeg\r\n\r\n")
        body.extend(jpeg)
        body.extend(f"\r\n--{boundary}--\r\n".encode())

        headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
        if self.device_id:
            headers["Device-Id"] = self.device_id
        if self.client_id:
            headers["Client-Id"] = self.client_id
        if self.vision_token:
            headers["Authorization"] = f"Bearer {self.vision_token}"
        request = Request(self.vision_url, data=bytes(body), headers=headers, method="POST")
        proxy = os.getenv("XIAOZHI_CAMERA_VISION_PROXY", "").strip()
        opener = build_opener(ProxyHandler({"http": proxy, "https": proxy}) if proxy else ProxyHandler())
        with opener.open(request, timeout=CAMERA_VISION_TIMEOUT) as response:
            raw = response.read(CAMERA_MAX_RESPONSE_BYTES + 1)
        if len(raw) > CAMERA_MAX_RESPONSE_BYTES:
            raise RuntimeError("vision response is too large")
        text = raw.decode("utf-8", "replace").strip()
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return {"result": text}

    async def _camera_take_photo(
        self,
        params: dict[str, Any],
        progress: ProgressCallback | None = None,
    ) -> dict[str, Any]:
        question = str(params.get("question", "")).strip()
        if not question:
            raise ValueError("question is required")
        if not self.vision_url:
            raise RuntimeError("vision capability is unavailable for this session")
        if progress:
            progress("camera\nCapturing photo...")
        jpeg = await self._capture_camera_jpeg()
        if progress:
            progress(f"camera\nCompressing {len(jpeg) // 1024} KB photo...")
        jpeg = await asyncio.to_thread(self._prepare_camera_upload_jpeg, jpeg)
        if self.camera_preview:
            self.camera_preview(jpeg)
        if progress:
            progress(f"camera\nAnalyzing {len(jpeg) // 1024} KB photo...")
        result = await asyncio.to_thread(self._explain_camera_jpeg, jpeg, question)
        if progress:
            progress("camera\nPhoto analysis completed")
        return result

    @staticmethod
    def _local_command_description() -> str:
        description = (
            "Run a local command on this XiaoZhi device and return stdout, stderr and exit code. "
            "Commands are restricted to the configured allowlist unless dangerous mode is enabled."
        )
        if LOCAL_COMMAND_ALLOW_DANGEROUS and LOCAL_COMMAND_SUDO_PASSWORD:
            description += (
                f" For sudo, pass {SUDO_PASSWORD_PLACEHOLDER} as a standalone argument immediately "
                "after sudo, for example: sudo {{SUDO_PASSWORD}} apt-get update. The device injects "
                "the password through stdin; the password value is never exposed to you."
            )
        return description

    def register(self, name: str, description: str, schema: dict[str, Any], handler: ToolHandler) -> None:
        self.tools[name] = (description, schema, handler)

    async def _end_activity_after_background_job(self, job: CommandJob) -> None:
        try:
            if job.monitor is not None:
                await asyncio.shield(job.monitor)
            else:
                await job.process.wait()
        finally:
            if self.command_activity:
                self.command_activity(False)

    async def handle(self, rpc: dict[str, Any]) -> dict[str, Any] | None:
        rpc_id = rpc.get("id")
        if rpc_id is None:
            return None
        method = str(rpc.get("method", ""))
        params = rpc.get("params") or {}
        if method == "initialize":
            self._configure_capabilities(params)
            result = {
                "protocolVersion": params.get("protocolVersion", "2024-11-05"),
                "capabilities": {"tools": {"listChanged": True}},
                "serverInfo": {"name": "cardputer-xiaozhi", "version": "0.2.7"},
            }
        elif method == "tools/list":
            result = {
                "tools": [
                    {"name": name, "description": item[0], "inputSchema": item[1]}
                    for name, item in self.tools.items()
                ]
            }
        else:
            name = str(params.get("name", method))
            arguments = params.get("arguments") or {}
            tool = self.tools.get(name)
            if tool is None:
                return {"jsonrpc": "2.0", "id": rpc_id, "result": {"error": f"Unknown tool: {name}"}}
            is_local_command = name == "local_command"
            background_activity = False
            if is_local_command and self.command_activity:
                self.command_activity(True)
                # Give the UI bridge enough time to stop the native watercolor
                # renderer before a memory-heavy child process is spawned.
                await asyncio.sleep(0.2)
            try:
                value = await tool[2](arguments, self.progress)
                if (
                    is_local_command
                    and self.command_activity
                    and isinstance(value, dict)
                    and value.get("status") == "running"
                ):
                    job = _JOBS.get(str(value.get("job_id", "")))
                    if job is not None:
                        background_activity = True
                        asyncio.create_task(self._end_activity_after_background_job(job))
                result = {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}]}
            except Exception as exc:
                if self.progress:
                    self.progress(f"error\n{_redact_secret(str(exc))}")
                result = {"error": _redact_secret(str(exc))}
            finally:
                if is_local_command and self.command_activity and not background_activity:
                    self.command_activity(False)
        return {"jsonrpc": "2.0", "id": rpc_id, "result": result}
