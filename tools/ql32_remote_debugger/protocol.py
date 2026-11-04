"""Codec for the line-framed QL/32 remote debugger control protocol."""

from __future__ import annotations

import json
from urllib.parse import quote, unquote

PREFIX = "@QL32DBG/1"


def encode_request(request_id: int, operation: str, *arguments: str) -> bytes:
    fields = [PREFIX, str(int(request_id)), operation]
    fields.extend(quote(str(argument), safe="") for argument in arguments)
    return ("\t".join(fields) + "\r").encode("ascii")


def decode_response(line: bytes | str) -> tuple[int, bool, object]:
    if isinstance(line, bytes):
        line = line.decode("utf-8", errors="replace")
    line = line.strip("\r\n")
    if not line.startswith(PREFIX + "\t"):
        raise ValueError("not a QL/32 debugger response")
    parts = line.split("\t", 3)
    if len(parts) != 4:
        raise ValueError("incomplete QL/32 debugger response")
    _, request_id, status, payload = parts
    if status not in ("OK", "ERR"):
        raise ValueError("unknown QL/32 debugger response status")
    return int(request_id), status == "OK", json.loads(payload)


def encode_response(request_id: int, ok: bool, payload: object) -> str:
    status = "OK" if ok else "ERR"
    return f"{PREFIX}\t{int(request_id)}\t{status}\t{json.dumps(payload, separators=(',', ':'))}\n"


def decode_argument(value: str) -> str:
    return unquote(value, encoding="utf-8", errors="strict")
