#!/usr/bin/env python3
"""Single-file QL/32 YMODEM utility (CRC-16, 1024-byte data blocks)."""

import argparse
import os
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)

SOH, STX, EOT, ACK, NAK, CAN, CRC_REQUEST = 1, 2, 4, 6, 0x15, 0x18, ord("C")
BLOCK = 1024
RETRIES = 10


def crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def read_byte(port, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        byte = port.read(1)
        if byte:
            return byte[0]
    return None


def read_exact(port, count, timeout=15):
    deadline = time.monotonic() + timeout
    result = bytearray()
    while len(result) < count and time.monotonic() < deadline:
        result.extend(port.read(count - len(result)))
    return bytes(result) if len(result) == count else None


def wait_for(port, expected, message):
    for _ in range(RETRIES):
        value = read_byte(port)
        if value == expected:
            return
        if value == CAN:
            raise RuntimeError("transfer cancelled by device")
    raise RuntimeError(message)


def packet(port, marker, sequence, payload):
    if len(payload) not in (128, BLOCK):
        raise ValueError("invalid YMODEM packet size")
    crc = crc16(payload)
    port.write(bytes((marker, sequence, 0xFF - sequence)) + payload +
               bytes((crc >> 8, crc & 0xFF)))


def read_packet(port, expected_size=None):
    marker = read_byte(port)
    if marker == CAN:
        raise RuntimeError("transfer cancelled by device")
    if marker not in (SOH, STX):
        return None
    size = 128 if marker == SOH else BLOCK
    if expected_size is not None and size != expected_size:
        return None
    header = read_exact(port, 2)
    payload = read_exact(port, size)
    checksum = read_exact(port, 2)
    if header is None or payload is None or checksum is None:
        return None
    sequence, complement = header
    expected_crc = crc16(payload)
    received_crc = (checksum[0] << 8) | checksum[1]
    if complement != (0xFF - sequence) or received_crc != expected_crc:
        return None
    return sequence, payload


def make_header(name, size):
    encoded = name.encode("ascii")
    metadata = encoded + b"\0" + str(size).encode("ascii") + b"\0"
    if not encoded or len(metadata) > 128:
        raise ValueError("filename or metadata does not fit YMODEM header")
    return metadata.ljust(128, b"\0")


def request_transfer(port, command, path):
    if path is None:
        return
    port.write(b"\r\n")
    deadline = time.monotonic() + 30
    prompt = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        prompt.extend(byte)
        if len(prompt) > 128:
            del prompt[:-128]
        if prompt.endswith(b"> "):
            break
    else:
        raise RuntimeError("device REPL prompt not received")
    escaped = path.replace('"', '""')
    port.write(f'{command} "{escaped}"\r'.encode("ascii"))
    marker = f"YMODEM READY {command}".encode("ascii")
    deadline = time.monotonic() + 30
    seen = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        seen.extend(byte)
        if len(seen) > 256:
            del seen[:-256]
        if marker in seen:
            return
        if b"Transfer failed:" in seen:
            raise RuntimeError(seen.decode("utf-8", errors="replace").strip())
    raise RuntimeError("device did not become ready for YMODEM")


def send_file(port, source, device_path=None):
    source = Path(source)
    size = source.stat().st_size
    request_transfer(port, "RECV", device_path)
    wait_for(port, CRC_REQUEST, "device did not start YMODEM receive")
    packet(port, SOH, 0, make_header(source.name, size))
    wait_for(port, ACK, "device did not acknowledge the file header")
    wait_for(port, CRC_REQUEST, "device did not request file data")
    sequence = 1
    remaining = size
    with source.open("rb") as stream:
        while remaining:
            block = stream.read(min(BLOCK, remaining))
            if not block:
                raise RuntimeError("source file changed during transfer")
            remaining -= len(block)
            payload = block.ljust(BLOCK, b"\x1a")
            acknowledged = False
            for _ in range(RETRIES):
                packet(port, STX, sequence & 0xFF, payload)
                response = read_byte(port)
                if response == ACK:
                    acknowledged = True
                    break
                if response == CAN:
                    raise RuntimeError("transfer cancelled by device")
            if not acknowledged:
                raise RuntimeError("data block retry limit exceeded")
            sequence += 1
            print(f"\rSent {size - remaining}/{size} bytes", end="", flush=True)
    port.write(bytes((EOT,)))
    wait_for(port, NAK, "device did not acknowledge first end-of-file")
    port.write(bytes((EOT,)))
    wait_for(port, ACK, "device did not acknowledge end-of-file")
    wait_for(port, CRC_REQUEST, "device did not request batch terminator")
    packet(port, SOH, 0, bytes(128))
    wait_for(port, ACK, "device did not acknowledge batch terminator")
    print(f"\nSent {source.name} ({size} bytes)")


def receive_file(port, destination, device_path=None):
    destination = Path(destination)
    temporary = destination.with_name(destination.name + ".part")
    request_transfer(port, "SEND", device_path)
    for _ in range(RETRIES * 3):
        port.write(bytes((CRC_REQUEST,)))
        frame = read_packet(port, 128)
        if frame is not None:
            break
    else:
        raise RuntimeError("device did not start YMODEM send")
    sequence, header = frame
    if sequence != 0 or header[0] == 0:
        raise RuntimeError("invalid YMODEM file header")
    name_end = header.index(0)
    name = header[:name_end].decode("ascii", errors="replace")
    size_start = name_end + 1
    size_end = header.find(0, size_start)
    if size_end < 0:
        size_end = 128
    size = int(header[size_start:size_end].split(b" ", 1)[0])
    port.write(bytes((ACK, CRC_REQUEST)))
    remaining = size
    expected = 1
    try:
        with temporary.open("wb") as stream:
            while remaining:
                frame = read_packet(port, BLOCK)
                if frame is None:
                    port.write(bytes((NAK,)))
                    continue
                sequence, payload = frame
                if sequence == ((expected - 1) & 0xFF):
                    port.write(bytes((ACK,)))
                    continue
                if sequence != (expected & 0xFF):
                    port.write(bytes((NAK,)))
                    continue
                count = min(remaining, BLOCK)
                stream.write(payload[:count])
                remaining -= count
                expected += 1
                port.write(bytes((ACK,)))
                print(f"\rReceived {size - remaining}/{size} bytes", end="", flush=True)
            if read_byte(port) != EOT:
                raise RuntimeError("device did not send end-of-file")
            port.write(bytes((NAK,)))
            if read_byte(port) != EOT:
                raise RuntimeError("device did not repeat end-of-file")
            port.write(bytes((ACK, CRC_REQUEST)))
            final = read_packet(port, 128)
            if final is None or final[0] != 0 or final[1][0] != 0:
                raise RuntimeError("invalid YMODEM batch terminator")
            port.write(bytes((ACK,)))
        os.replace(temporary, destination)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        port.write(bytes((CAN, CAN)))
        raise
    print(f"\nReceived {name} as {destination} ({size} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    for operation in ("send", "receive"):
        sub = subparsers.add_parser(operation)
        sub.add_argument("port", help="same serial port used by the QL/32 REPL")
        sub.add_argument("baud", type=int, help="same baud rate used by the REPL")
        sub.add_argument("file", help="local source file for send; destination for receive")
        sub.add_argument("--device-path", help="device path; utility enters RECV or SEND for you")
    args = parser.parse_args()
    try:
        port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                             write_timeout=10)
        port.port = args.port
        port.dtr = False
        port.rts = False
        with port:
            if args.operation == "send":
                send_file(port, args.file, args.device_path)
            else:
                receive_file(port, args.file, args.device_path)
    except Exception as error:
        print(f"ql32-ymodem: {error}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
