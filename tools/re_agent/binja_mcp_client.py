#!/usr/bin/env python3
"""
Binary Ninja MCP Client

Client for interacting with Binary Ninja MCP servers to decompile functions
and perform smart diff analysis on MIPS binaries.

This integrates with the existing Binary Ninja MCP smart diff tooling.
"""

import json
import os
import urllib.parse
import urllib.request
import time
import uuid
import threading
from collections import deque
from typing import Dict, List, Optional, Any
from dataclasses import dataclass


@dataclass
class BinaryInfo:
    """Information about a loaded binary"""
    binary_id: str
    name: str
    architecture: str
    base_address: int


@dataclass
class FunctionInfo:
    """Information about a function in a binary"""
    name: str
    address: int
    size: int
    binary_id: str


@dataclass
class FunctionDiff:
    """Difference between two function versions"""
    function_name: str
    source_code: str
    target_code: str
    similarity: float
    changes: List[str]


class BinaryNinjaMCPClient:
    """
    Client for Binary Ninja MCP servers.

    This client assumes Binary Ninja MCP servers are running and accessible.
    It provides methods to:
    - List available binaries
    - Decompile functions
    - Compare binary versions
    - Perform smart diff analysis
    """

    def __init__(self, mcp_command: str = "mcp", base_url: Optional[str] = None):
        """
        Initialize the MCP client.

        Args:
            mcp_command: Command to invoke MCP CLI (if available)
            base_url: Optional HTTP base URL for the Smart-Diff MCP aggregator
        """
        self.mcp_command = mcp_command
        # Allow config via env; fallback to provided base_url; else None (hardcoded stubs)
        self.base_url = os.environ.get("BN_MCP_BASE_URL") or os.environ.get("SMART_DIFF_BASE_URL") or base_url
        self.available_binaries: Dict[str, BinaryInfo] = {}
        # SSE background reader state
        self._sse_thread: Optional[threading.Thread] = None
        self._sse_events = deque(maxlen=500)
        self._sse_cond = threading.Condition()
        self._sse_running = False

    def _http_get_json(self, path: str, params: Optional[Dict[str, str]] = None) -> Optional[Any]:
        if not self.base_url:
            return None
        url = self.base_url.rstrip("/") + "/" + path.lstrip("/")
        if params:
            url += ("?" + urllib.parse.urlencode(params))
        try:
            with urllib.request.urlopen(url, timeout=8) as resp:
                data = resp.read()
                return json.loads(data.decode("utf-8"))
        except Exception as e:
            print(f"[MCP http] GET {url} failed: {e}")
            return None

    def _http_get_text(self, path: str, params: Optional[Dict[str, str]] = None) -> Optional[str]:
        if not self.base_url:
            return None
        url = self.base_url.rstrip("/") + "/" + path.lstrip("/")
        if params:
            url += ("?" + urllib.parse.urlencode(params))
        try:
            with urllib.request.urlopen(url, timeout=15) as resp:
                return resp.read().decode("utf-8")
        except Exception as e:
            print(f"[MCP http] GET {url} failed: {e}")
            return None


    def _resolve_binary_id(self, binary_id: str) -> str:
        """Resolve provided binary_id to an active bridge server id when using the multi-binary bridge.
        Heuristics:
        - If binary_id matches a known server id or name, use it
        - If only one server is active, use that id
        - If 'port_XXXX' is provided and exactly one server is active, use that id
        """
        if not self.base_url:
            return binary_id
        # If already cached
        if binary_id in self.available_binaries:
            return binary_id
        try:
            servers = self.list_binaries()
        except Exception:
            servers = []
        if servers:
            # Exact id or name match
            for b in servers:
                if b.binary_id == binary_id or b.name == binary_id:
                    return b.binary_id
            # Heuristics when only one server is active
            if binary_id.startswith("port_") and len(servers) == 1:
                return servers[0].binary_id
            if len(servers) == 1:
                return servers[0].binary_id
        return binary_id

    def _direct_base_from_binary_id(self, binary_id: str) -> Optional[str]:
        """Derive a direct BN server base URL from a binary_id like 'port_9009' or '9009' or full URL.
        Returns http://localhost:<port> or the provided URL if binary_id looks like a URL.
        """
        bid = str(binary_id or "").strip()
        if bid.startswith("http://") or bid.startswith("https://"):
            return bid.rstrip("/")
        # Accept patterns: port_9009, 9009
        port: Optional[int] = None
        if bid.startswith("port_"):
            try:
                port = int(bid.split("_")[-1])
            except Exception:
                port = None
        elif bid.isdigit():
            try:
                port = int(bid)
            except Exception:
                port = None
        if port:
            return f"http://localhost:{port}"
        return None

    def _http_get_json_full(self, url: str) -> Optional[Any]:
        try:
            with urllib.request.urlopen(url, timeout=10) as resp:
                data = resp.read()
                return json.loads(data.decode("utf-8"))
        except Exception as e:
            print(f"[MCP http] GET {url} failed: {e}")
            return None

    def _http_post_json_full(self, url: str, body: Dict[str, Any]) -> Optional[Any]:
        try:
            data = json.dumps(body).encode("utf-8")
            req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=15) as resp:
                raw = resp.read().decode("utf-8")
                return json.loads(raw) if raw.strip() else None
        except Exception as e:
            print(f"[MCP http] POST {url} failed: {e}")
            return None

    def _ensure_sse_background(self):
        """Start a single background SSE reader if base_url is set and not running."""
        if not self.base_url:
            return
        if self._sse_thread and self._sse_thread.is_alive():
            return
        self._sse_running = True
        self._sse_thread = threading.Thread(target=self._sse_background, name="BN-MCP-SSE", daemon=True)
        self._sse_thread.start()

    def _enqueue_event(self, obj: Any):
        with self._sse_cond:
            self._sse_events.append((time.time(), obj))
            self._sse_cond.notify_all()

    def _sse_background(self):
        """Persistent SSE reader that feeds events into a local queue for correlation."""
        if not self.base_url:
            return
        url = self.base_url.rstrip("/") + "/sse"
        while self._sse_running:
            start = time.time()
            buf = ""
            try:
                with urllib.request.urlopen(url, timeout=60) as resp:
                    while self._sse_running:
                        try:
                            chunk = resp.readline().decode("utf-8", errors="ignore")
                        except OSError as oe:
                            if getattr(oe, "errno", None) in (11, 115):
                                time.sleep(0.05)
                                continue
                            raise
                        if not chunk:
                            time.sleep(0.05)
                            continue
                        if chunk.strip() == "":
                            if buf:
                                # parse event
                                data_lines = [ln[5:].strip() for ln in buf.splitlines() if ln.startswith("data:")]
                                if data_lines:
                                    try:
                                        obj = json.loads("\n".join(data_lines))
                                        self._enqueue_event(obj)
                                    except Exception:
                                        pass
                                buf = ""
                            continue
                        buf += chunk
            except Exception as e:
                print(f"[MCP sse-bg] reconnecting after error: {e}")
                # brief backoff then reconnect
                time.sleep(0.5)

        # loop will reconnect; exit when _sse_running is cleared
        return

    def _bridge_call(self, method: str, params: Optional[Dict[str, Any]] = None) -> Optional[Any]:
        """Call the SSE bridge /message endpoint if available.
        Accepts both simple {"method","params"}, JSON-RPC {"jsonrpc","id","method","params"},
        and a few alternate shapes seen in MCP bridges.
        Returns parsed JSON (result if JSON-RPC), or the raw string.
        """
        if not self.base_url:
            return None
        url = self.base_url.rstrip("/") + "/message"
        payloads = [
            {"method": method, "params": params or {}},
            {"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}},
            {"tool": method, "params": params or {}},
            {"name": method, "arguments": params or {}},
            {"function": method, "args": params or {}},
        ]
        last_error: Optional[str] = None
        for body in payloads:
            try:
                data = json.dumps(body).encode("utf-8")
                req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
                with urllib.request.urlopen(req, timeout=20) as resp:
                    status = getattr(resp, 'status', None) or resp.getcode()
                    raw = resp.read().decode("utf-8")
                    # Debug: show a snippet of the raw response for visibility
                    preview = raw.replace("\n", " ")[:200]
                    print(f"[MCP bridge] POST {url} method={method} -> {status} body~ {preview}")
                    if status and status >= 400:
                        last_error = f"HTTP {status}: {preview}"
                        continue
                    # Try JSON
                    try:
                        obj = json.loads(raw)
                    except Exception:
                        # Non-JSON body; return raw text if present
                        return raw if raw.strip() else None
                    # Normalize JSON-RPC shape
                    if isinstance(obj, dict) and obj.get("jsonrpc") == "2.0":
                        if "result" in obj:
                            return obj["result"]
                        if "error" in obj:
                            last_error = f"JSON-RPC error: {obj['error']}"
                            continue
                    return obj
            except urllib.error.HTTPError as he:  # type: ignore
                try:
                    body = he.read().decode("utf-8")
                except Exception:
                    body = str(he)

                    body = he.read().decode("utf-8")
                except Exception:
                    body = str(he)
                last_error = f"HTTPError {he.code}: {body[:200]}"
                continue
            except Exception as e:
                last_error = str(e)
                continue
    def _sse_iter(self, timeout: int = 10):
        """Iterate Server-Sent Events from /sse for up to timeout seconds."""
        if not self.base_url:
            return
        url = self.base_url.rstrip("/") + "/sse"
        start = time.time()
        buf = ""
        try:
            with urllib.request.urlopen(url, timeout=timeout) as resp:
                while time.time() - start < timeout:
                    try:
                        chunk = resp.readline().decode("utf-8", errors="ignore")
                    except OSError as oe:
                        # EAGAIN/EINPROGRESS on some bridges; brief backoff
                        if getattr(oe, "errno", None) in (11, 115):
                            time.sleep(0.05)
                            continue
                        raise
                    if not chunk:
                        # allow a moment for more data to arrive
                        time.sleep(0.05)
                        continue
                    if chunk.strip() == "":
                        # end of one event
                        if buf:
                            yield buf
                            buf = ""
                        continue
                    buf += chunk
            # flush any trailing partial event
            if buf:
                yield buf
        except Exception as e:
            print(f"[MCP sse] GET {url} failed: {e}")
            return

    def _sse_wait_for(self, expect_method: Optional[str], expect_params: Optional[Dict[str, Any]],
                      desired_keys: List[str], function_name: Optional[str] = None, timeout: int = 10) -> Optional[Any]:
        """Wait on /sse for an event carrying desired_keys. Heuristics:
        - if JSON object has 'method' equal to expect_method, prefer it
        - if function_name is provided, prefer events mentioning it
        - extract first matching key among desired_keys
        """
        self._ensure_sse_background()
        end = time.time() + timeout
        last_checked = 0
        while time.time() < end:
            with self._sse_cond:
                now = time.time()
                for ts, obj in list(self._sse_events):
                    if ts <= last_checked:
                        continue
                    payload = obj
                    if isinstance(obj, dict) and "result" in obj and obj.get("jsonrpc") == "2.0":
                        payload = obj["result"]
                    if isinstance(payload, dict):
                        method = payload.get("method") or payload.get("tool") or payload.get("name") or payload.get("function")
                        if expect_method and method and method != expect_method:
                            continue
                        for k in desired_keys:
                            if k in payload and isinstance(payload[k], (str, list)):
                                val = payload[k]
                                if function_name and isinstance(val, str) and function_name not in val:
                                    continue
                                return val
                        data_obj = payload.get("data") if isinstance(payload.get("data"), dict) else None
                        if data_obj:
                            for k in desired_keys:
                                if k in data_obj and isinstance(data_obj[k], (str, list)):
                                    val = data_obj[k]
                                    if function_name and isinstance(val, str) and function_name not in val:
                                        continue
                                    return val
                    elif isinstance(payload, list) and any(isinstance(x, str) for x in payload):
                        return payload
                last_checked = now
                remaining = max(0.05, end - time.time())
                self._sse_cond.wait(timeout=min(remaining, 1.0))
        return None
    def _sse_wait_for_id(self, req_id: str, desired_keys: List[str], function_name: Optional[str] = None, timeout: int = 15) -> Optional[Any]:
        """Wait for a JSON-RPC SSE event with matching id and extract desired_keys from result.
        Falls back to extracting directly from payload if structured differently.
        """
        end = time.time() + timeout
        self._ensure_sse_background()
        # First, scan any queued events then wait for new ones
        end = time.time() + timeout
        last_checked = 0
        while time.time() < end:
            with self._sse_cond:
                # Scan queue
                now = time.time()
                for ts, obj in list(self._sse_events):
                    if ts <= last_checked:
                        continue
                    # JSON-RPC envelope
                    if isinstance(obj, dict) and obj.get("jsonrpc") == "2.0" and obj.get("id") == req_id:
                        payload = obj.get("result")
                        if isinstance(payload, dict):
                            for k in desired_keys:
                                if k in payload and isinstance(payload[k], (str, list)):
                                    val = payload[k]
                                    if function_name and isinstance(val, str) and function_name not in val:
                                        continue
                                    return val
                            data_obj = payload.get("data") if isinstance(payload.get("data"), dict) else None
                            if data_obj:
                                for k in desired_keys:
                                    if k in data_obj and isinstance(data_obj[k], (str, list)):
                                        val = data_obj[k]
                                        if function_name and isinstance(val, str) and function_name not in val:
                                            continue
                                        return val
                        elif isinstance(payload, (str, list)):
                            return payload
                    # Non-JSON-RPC but with id
                    if isinstance(obj, dict) and obj.get("id") == req_id:
                        payload = obj.get("result") or obj
                        if isinstance(payload, dict):
                            for k in desired_keys:
                                if k in payload and isinstance(payload[k], (str, list)):
                                    return payload[k]
                        elif isinstance(payload, (str, list)):
                            return payload
                last_checked = now
                remaining = max(0.05, end - time.time())
                self._sse_cond.wait(timeout=min(remaining, 1.0))
        return None


    def list_binaries(self) -> List[BinaryInfo]:
        """
        List all binaries loaded in Binary Ninja MCP servers.

        Returns:
            List of BinaryInfo objects
        """
        # Ensure SSE is up before posting so the bridge can deliver async responses
        self._ensure_sse_background()
        # Try SSE bridge first via /message
        servers: List[BinaryInfo] = []
        if self.base_url:
            # Prefer JSON-RPC list methods first
            for method in ("list_binary_servers", "list_binja_servers", "list_binja_servers_smart-diff"):
                try:
                    req_id = str(uuid.uuid4())
                    sync = self._bridge_call_jsonrpc(method, {}, req_id)
                    candidate = sync
                    if not candidate:
                        candidate = self._bridge_call(method, {})
                    if isinstance(candidate, list) and candidate:
                        try:
                            for it in candidate:
                                bid = (it.get("id") or it.get("binary_id") or it.get("server_id") or it.get("name"))
                                name = it.get("name") or it.get("title") or bid
                                arch = it.get("architecture") or it.get("arch") or "mips32"
                                base = it.get("base_address") or 0
                                if bid:
                                    servers.append(BinaryInfo(binary_id=str(bid), name=str(name), architecture=str(arch), base_address=int(base)))
                            if servers:
                                break
                        except Exception as e:
                            print(f"[MCP] Failed to parse servers from bridge: {e}")
                except Exception:
                    pass
            # As a last resort, try a generic SSE scan for these methods
            if not servers:
                for ev_method in ("list_binary_servers", "list_binja_servers", "list_binja_servers_smart-diff"):
                    sse_res = self._sse_wait_for(ev_method, {}, desired_keys=["servers", "binaries", "list"], timeout=6)
                    if isinstance(sse_res, list) and sse_res:
                        try:
                            for it in sse_res:
                                if isinstance(it, dict):
                                    bid = (it.get("id") or it.get("binary_id") or it.get("server_id") or it.get("name"))
                                    name = it.get("name") or it.get("title") or bid
                                    arch = it.get("architecture") or it.get("arch") or "mips32"
                                    base = it.get("base_address") or 0
                                    if bid:
                                        servers.append(BinaryInfo(binary_id=str(bid), name=str(name), architecture=str(arch), base_address=int(base)))
                            if servers:
                                break
                        except Exception as e:
                            print(f"[MCP] Failed to parse servers from SSE: {e}")
        # Try simple REST-style endpoints as fallback
        if not servers and self.base_url:
            for endpoint in ("servers", "list_binja_servers", "api/servers"):
                data = self._http_get_json(endpoint)
                if isinstance(data, list) and data:
                    try:
                        for it in data:
                            bid = it.get("id") or it.get("binary_id") or it.get("name")
                            name = it.get("name") or it.get("title") or bid
                            arch = it.get("architecture") or it.get("arch") or "mips32"
                            base = it.get("base_address") or 0
                            if bid:
                                servers.append(BinaryInfo(binary_id=str(bid), name=str(name), architecture=str(arch), base_address=int(base)))
                        break
                    except Exception as e:
                        print(f"[MCP] Failed to parse servers response: {e}")
        if servers:
            self.available_binaries = {b.binary_id: b for b in servers}
            return servers

        # Fallback: known list
        known_binaries = [
            BinaryInfo(binary_id="port_9009", name="libimp.so (T31 v1.1.6)", architecture="mips32", base_address=0x0),
            BinaryInfo(binary_id="port_9012", name="libimp.so (T23)", architecture="mips32", base_address=0x0),
            BinaryInfo(binary_id="port_9013", name="tx-isp-t23.ko", architecture="mips32", base_address=0x0),
        ]
        for b in known_binaries:
            self.available_binaries[b.binary_id] = b
        return known_binaries

    def _bridge_call_jsonrpc(self, method: str, params: Optional[Dict[str, Any]], req_id: str) -> Optional[Any]:
        """POST a JSON-RPC shaped message with a specific id to /message and return parsed response if any."""
        if not self.base_url:
            return None
        url = self.base_url.rstrip("/") + "/message"
        body = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params or {}}
        try:
            data = json.dumps(body).encode("utf-8")
            req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=20) as resp:
                status = getattr(resp, 'status', None) or resp.getcode()
                raw = resp.read().decode("utf-8")
                preview = raw.replace("\n", " ")[:200]
                print(f"[MCP bridge] POST {url} method={method} id={req_id} -> {status} body~ {preview}")
                if status and status >= 400:
                    return None
                # If a synchronous JSON-RPC response arrives
                try:
                    obj = json.loads(raw)
                    if isinstance(obj, dict) and obj.get("jsonrpc") == "2.0" and obj.get("id") == req_id:
                        return obj.get("result")
                    return obj
                except Exception:
                    return raw if raw.strip() else None
        except Exception as e:
            print(f"[MCP bridge] JSON-RPC POST {url} method={method} id={req_id} failed: {e}")
            return None


    def decompile_function(self, binary_id: str, function_name: str) -> Optional[str]:
        """
        Decompile a function from a binary.

        Args:
            binary_id: ID of the binary (e.g., "port_9009")
            function_name: Name of the function to decompile
        """
        # Ensure SSE is up before posting so the bridge can deliver async responses
        self._ensure_sse_background()

        # Direct BN server path (bypass bridge): use Binary Ninja plugin HTTP endpoints
        direct_base = self._direct_base_from_binary_id(binary_id)
        if direct_base:
            # Try GET first
            q = urllib.parse.urlencode({"name": function_name})
            url = f"{direct_base}/decompile?{q}"
            obj = self._http_get_json_full(url)
            if isinstance(obj, dict):
                code = obj.get("decompiled") or obj.get("decompiled_code") or obj.get("code") or obj.get("text")
                if isinstance(code, str) and code.strip():
                    return code
            # Try POST fallback with JSON
            obj = self._http_post_json_full(f"{direct_base}/decompile", {"name": function_name})
            if isinstance(obj, dict):
                code = obj.get("decompiled") or obj.get("decompiled_code") or obj.get("code") or obj.get("text")
                if isinstance(code, str) and code.strip():
                    return code

        print(f"[MCP] Decompiling {function_name} from {binary_id}...")
        # Try SSE bridge first via /message
        if self.base_url:
            resolved_id = self._resolve_binary_id(binary_id)
            # Prefer JSON-RPC with correlation id, then wait on /sse for that id
            tried_methods = [
                ("decompile_binary_function_smart-diff", {"binary_id": resolved_id, "function_name": function_name}),
                ("decompile_binary_function_smart_diff", {"binary_id": resolved_id, "function_name": function_name}),
                ("decompile_binary_function", {"binary_id": resolved_id, "function_name": function_name}),
                ("decompile", {"binary_id": resolved_id, "function": function_name}),
            ]
            for method, params in tried_methods:
                req_id = str(uuid.uuid4())
                sync_res = self._bridge_call_jsonrpc(method, params, req_id)
                # If server replied synchronously, try to parse immediately
                if isinstance(sync_res, dict):
                    code = sync_res.get("decompiled_code") or sync_res.get("code") or sync_res.get("text")
                    if code and str(code).strip():
                        return str(code)
                elif isinstance(sync_res, str) and sync_res.strip():
                    return sync_res
                # Otherwise, await SSE event tied to this id
                sse_res = self._sse_wait_for_id(req_id, desired_keys=["decompiled_code", "code", "text"], function_name=function_name, timeout=20)
                if isinstance(sse_res, str) and sse_res.strip():
                    return sse_res
                if isinstance(sse_res, dict):
                    code = sse_res.get("decompiled_code") or sse_res.get("code") or sse_res.get("text")
                    if code and str(code).strip():
                        return str(code)
                # Finally, as a last resort, try generic POST then generic SSE scan
                res = self._bridge_call(method, params)
                if isinstance(res, dict):
                    code = res.get("decompiled_code") or res.get("code") or res.get("text")
                    if code and str(code).strip():
                        return str(code)
                elif isinstance(res, str) and res.strip():
                    return res
                sse_any = self._sse_wait_for(method, params, desired_keys=["decompiled_code", "code", "text"], function_name=function_name, timeout=8)
                if isinstance(sse_any, str) and sse_any.strip():
                    return sse_any
                if isinstance(sse_any, dict):
                    code = sse_any.get("decompiled_code") or sse_any.get("code") or sse_any.get("text")
                    if code and str(code).strip():
                        return str(code)
        # Try simple REST-style endpoints as fallback
        if self.base_url:
            candidates = [
                (f"binaries/{binary_id}/decompile", {"function": function_name}),
                (f"{binary_id}/decompile", {"function": function_name}),
                ("decompile", {"binary_id": binary_id, "function": function_name}),
            ]
            for path, params in candidates:
                txt = self._http_get_text(path, params)
                if txt and len(txt.strip()) > 0:
                    return txt
        return None

    def list_functions(self, binary_id: str, search: Optional[str] = None) -> List[str]:
        """
        List all functions in a binary.

        Args:
            binary_id: ID of the binary
            search: Optional search term to filter functions

        Returns:
            List of function names
        """
        # Direct BN server path (bypass bridge)
        direct_base = self._direct_base_from_binary_id(binary_id)
        if direct_base:
            obj = self._http_get_json_full(f"{direct_base}/functions")
            if isinstance(obj, dict):
                maybe = obj.get("functions") or obj.get("methods") or obj.get("names")
                if isinstance(maybe, list):
                    return [str(x.get("name") if isinstance(x, dict) else x) for x in maybe]

        # Try SSE bridge first via /message
        if self.base_url:
            resolved_id = self._resolve_binary_id(binary_id)
            # Prefer JSON-RPC with correlation id, try known method names
            for method in ("list_binary_functions_smart-diff", "list_binary_functions_smart_diff", "list_functions", "list_binary_functions"):
                req_id = str(uuid.uuid4())
                sync_res = self._bridge_call_jsonrpc(method, {"binary_id": resolved_id, "search": search} if search else {"binary_id": resolved_id}, req_id)
                # If we got a synchronous result
                if isinstance(sync_res, list):
                    return [str(x.get("name") or x.get("symbol") or x) for x in sync_res]
                if isinstance(sync_res, dict):
                    maybe = sync_res.get("functions") or sync_res.get("names")
                    if isinstance(maybe, list):
                        return [str(x.get("name") or x.get("symbol") or x) for x in maybe]
                # Await SSE for this id
                sse_res = self._sse_wait_for_id(req_id, desired_keys=["functions", "names", "symbols"], timeout=10)
                if isinstance(sse_res, list):
                    names: List[str] = []
                    for it in sse_res:
                        if isinstance(it, str):
                            names.append(it)
                        elif isinstance(it, dict):
                            n = it.get("name") or it.get("symbol")
                            if n:
                                names.append(str(n))
                    if names:
                        return names
            # Fallback to generic POST then generic SSE
            res = None
            for method in ("list_binary_functions_smart-diff", "list_binary_functions_smart_diff", "list_functions", "list_binary_functions"):
                res = self._bridge_call(method, {"binary_id": resolved_id, "search": search} if search else {"binary_id": resolved_id})
                if res:
                    break
            if isinstance(res, list):
                names: List[str] = []
                for it in res:
                    if isinstance(it, str):
                        names.append(it)
                    elif isinstance(it, dict):
                        n = it.get("name") or it.get("symbol")
                        if n:
                            names.append(str(n))
                if names:
                    return names
            # Try generic SSE scan for either event name
            for ev_method in ("list_binary_functions_smart-diff", "list_binary_functions_smart_diff", "list_functions", "list_binary_functions"):
                sse_names = self._sse_wait_for(ev_method, {"binary_id": resolved_id}, desired_keys=["functions", "names", "symbols"], timeout=6)
                if isinstance(sse_names, list):
                    names: List[str] = []
                    for it in sse_names:
                        if isinstance(it, str):
                            names.append(it)
                        elif isinstance(it, dict):
                            n = it.get("name") or it.get("symbol")
                            if n:
                                names.append(str(n))
                    if names:
                        return names
        # Try REST-style endpoints as fallback
        if self.base_url:
            params = {"search": search} if search else None
            for path in (f"binaries/{binary_id}/functions", f"{binary_id}/functions"):
                data = self._http_get_json(path, params)
                if isinstance(data, list):
                    names: List[str] = []
                    for it in data:
                        if isinstance(it, str):
                            names.append(it)
                        elif isinstance(it, dict):
                            n = it.get("name") or it.get("symbol")
                            if n:
                                names.append(str(n))
                    if names:
                        return names
        print("[MCP] Falling back to static function list (bridge/REST did not return functions)")
        # Fallback to static list
        imp_functions = [
            "IMP_System_Init","IMP_System_Exit","IMP_System_GetVersion","IMP_System_Bind","IMP_System_UnBind",
            "IMP_Encoder_CreateGroup","IMP_Encoder_DestroyGroup","IMP_Encoder_CreateChn","IMP_Encoder_DestroyChn",
            "IMP_Encoder_RegisterChn","IMP_Encoder_UnRegisterChn","IMP_Encoder_StartRecvPic","IMP_Encoder_StopRecvPic",
            "IMP_Encoder_Query","IMP_Encoder_GetStream","IMP_Encoder_ReleaseStream","IMP_FrameSource_CreateChn",
            "IMP_FrameSource_DestroyChn","IMP_FrameSource_SetChnAttr","IMP_FrameSource_GetChnAttr","IMP_FrameSource_EnableChn",
            "IMP_FrameSource_DisableChn","IMP_FrameSource_SetFrameDepth","IMP_FrameSource_GetFrameDepth","IMP_ISP_Open",
            "IMP_ISP_Close","IMP_ISP_AddSensor","IMP_ISP_DelSensor","IMP_ISP_EnableSensor","IMP_ISP_DisableSensor",
            "IMP_ISP_SetSensorRegister","IMP_ISP_GetSensorRegister","IMP_ISP_Tuning_SetContrast","IMP_ISP_Tuning_GetContrast",
            "IMP_ISP_Tuning_SetBrightness","IMP_ISP_Tuning_GetBrightness","IMP_ISP_Tuning_SetSaturation","IMP_ISP_Tuning_GetSaturation",
            "IMP_ISP_Tuning_SetSharpness","IMP_ISP_Tuning_GetSharpness","IMP_OSD_CreateGroup","IMP_OSD_DestroyGroup",
            "IMP_OSD_RegisterRgn","IMP_OSD_UnRegisterRgn","IMP_OSD_SetRgnAttr","IMP_OSD_GetRgnAttr","IMP_OSD_ShowRgn","IMP_OSD_HideRgn",
            "IMP_IVS_CreateGroup","IMP_IVS_DestroyGroup","IMP_IVS_CreateChn","IMP_IVS_DestroyChn","IMP_IVS_RegisterChn","IMP_IVS_UnRegisterChn",
            "IMP_IVS_StartRecvPic","IMP_IVS_StopRecvPic","IMP_IVS_PollingResult","IMP_IVS_GetResult","IMP_IVS_ReleaseResult",
        ]
        if search:
            imp_functions = [f for f in imp_functions if search.lower() in f.lower()]
        return imp_functions

    def compare_binaries(self, binary_a_id: str, binary_b_id: str,
                        similarity_threshold: float = 0.7,
                        use_decompiled_code: bool = True) -> str:
        """
        Compare two binaries and identify matching functions.

        Args:
            binary_a_id: First binary ID
            binary_b_id: Second binary ID
            similarity_threshold: Minimum similarity threshold (0.0 to 1.0)
            use_decompiled_code: Whether to use decompiled code for comparison

        Returns:
            Comparison ID for querying results
        """
        # This would call the actual MCP compare_binaries tool
        print(f"[MCP] Comparing {binary_a_id} with {binary_b_id}...")

        # In real implementation, this would call:
        # result = mcp_call("compare_binaries", {
        #     "binary_a_id": binary_a_id,
        #     "binary_b_id": binary_b_id,
        #     "similarity_threshold": similarity_threshold,
        #     "use_decompiled_code": use_decompiled_code
        # })
        # return result["comparison_id"]

        return "comparison_placeholder"

    def get_function_diff(self, comparison_id: str, function_name: str) -> Optional[FunctionDiff]:
        """
        Get detailed diff for a specific function match.

        Args:
            comparison_id: The comparison ID from compare_binaries
            function_name: Name of the function to get diff for

        Returns:
            FunctionDiff object or None if not found
        """
        # This would call the actual MCP get_binary_function_diff tool
        print(f"[MCP] Getting diff for {function_name} in comparison {comparison_id}...")

        # In real implementation, this would call:
        # result = mcp_call("get_binary_function_diff", {
        #     "comparison_id": comparison_id,
        #     "function_name": function_name
        # })
        # return FunctionDiff(**result)

        return None

    def analyze_function_diff(self, comparison_id: str, function_name: str,
                             include_constants: bool = True,
                             include_api_calls: bool = True,
                             include_control_flow: bool = True) -> Dict[str, Any]:
        """
        Get enhanced analysis of binary function differences.

        Args:
            comparison_id: The comparison ID
            function_name: Name of the function to analyze
            include_constants: Include constant/magic number analysis
            include_api_calls: Include API call analysis
            include_control_flow: Include control flow analysis

        Returns:
            Dictionary containing detailed analysis
        """
        # This would call the actual MCP analyze_binary_function_diff tool
        print(f"[MCP] Analyzing diff for {function_name}...")

        # In real implementation, this would call:
        # result = mcp_call("analyze_binary_function_diff", {
        #     "comparison_id": comparison_id,
        #     "function_name": function_name,
        #     "include_constants": include_constants,
        #     "include_api_calls": include_api_calls,
        #     "include_control_flow": include_control_flow
        # })
        # return result

        return {}

    def search_functions(self, comparison_id: str, query: str,
                        fuzzy: bool = True, max_results: int = 100) -> List[str]:
        """
        Search for functions across a binary comparison.

        Args:
            comparison_id: The comparison ID
            query: Function name or partial name to search for
            fuzzy: Enable fuzzy matching
            max_results: Maximum number of results

        Returns:
            List of matching function names
        """
        # This would call the actual MCP search_binary_functions tool
        print(f"[MCP] Searching for functions matching '{query}'...")

        # In real implementation, this would call:
        # result = mcp_call("search_binary_functions", {
        #     "comparison_id": comparison_id,
        #     "query": query,
        #     "fuzzy": fuzzy,
        #     "max_results": max_results
        # })
        # return result["matches"]

        return []


class SmartDiffAnalyzer:
    """
    High-level analyzer that combines Binary Ninja MCP with AI analysis.

    This class provides convenient methods for common reverse engineering tasks.
    """

    def __init__(self, mcp_client: BinaryNinjaMCPClient):
        """Initialize with an MCP client"""
        self.mcp = mcp_client

    def analyze_struct_offsets(self, binary_id: str, function_name: str) -> Dict[str, Any]:
        """
        Analyze a function to extract struct offset information.

        Args:
            binary_id: Binary to analyze
            function_name: Function to decompile and analyze

        Returns:
            Dictionary containing offset information
        """
        decompiled = self.mcp.decompile_function(binary_id, function_name)
        if not decompiled:
            return {"error": "Failed to decompile function"}

        # Extract offset patterns like: ptr + 0x10, *(ptr + 0x20), etc.
        import re
        offset_pattern = r'(?:ptr|\w+)\s*\+\s*0x([0-9a-fA-F]+)'
        offsets = re.findall(offset_pattern, decompiled)

        unique_offsets = sorted(set(int(o, 16) for o in offsets))

        return {
            "function": function_name,
            "decompiled_code": decompiled,
            "offsets": [f"0x{o:04x}" for o in unique_offsets],
            "offset_count": len(unique_offsets)
        }

    def compare_function_versions(self, old_binary_id: str, new_binary_id: str,
                                 function_name: str) -> Dict[str, Any]:
        """
        Compare two versions of the same function.

        Args:
            old_binary_id: Old binary version
            new_binary_id: New binary version
            function_name: Function to compare

        Returns:
            Dictionary containing comparison results
        """
        old_code = self.mcp.decompile_function(old_binary_id, function_name)
        new_code = self.mcp.decompile_function(new_binary_id, function_name)

        if not old_code or not new_code:
            return {"error": "Failed to decompile one or both functions"}

        return {
            "function": function_name,
            "old_binary": old_binary_id,
            "new_binary": new_binary_id,
            "old_code": old_code,
            "new_code": new_code,
            "changed": old_code != new_code
        }

