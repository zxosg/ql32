"""Single-owner serial thread for debugger requests and YMODEM transfers."""

from __future__ import annotations

import queue
import threading
import time
from pathlib import Path

from PySide6.QtCore import QThread, Signal

from tools import ql32_ymodem
from .protocol import PREFIX, decode_response, encode_request


class SerialWorker(QThread):
    connected = Signal(str, dict)
    connection_failed = Signal(str)
    disconnected = Signal()
    log_message = Signal(str)
    repl_output = Signal(str)
    request_finished = Signal(int, bool, object)
    transfer_progress = Signal(str, int, int)
    transfer_status = Signal(str, str)
    transfer_finished = Signal(str, str, str, str)

    def __init__(self):
        super().__init__()
        self._commands: queue.Queue[tuple] = queue.Queue()
        self._next_id = 1
        self._id_lock = threading.Lock()
        self._serial = None
        self._line_buffer = bytearray()
        self._rx_buffer = bytearray()
        self._cancel_transfer = threading.Event()
        self._supported = False
        self._closing = False

    def connect_port(self, port: str, baud: int = 115200) -> None:
        self.start_if_needed()
        self._commands.put(("connect", port, baud))

    def disconnect_port(self) -> None:
        self._commands.put(("disconnect",))

    def start_if_needed(self) -> None:
        if not self.isRunning():
            self.start()

    def request(self, operation: str, *arguments: str) -> int:
        with self._id_lock:
            request_id = self._next_id
            self._next_id += 1
        self._commands.put(("request", request_id, operation, tuple(map(str, arguments))))
        return request_id

    def send_repl_command(self, command: str) -> None:
        self._commands.put(("repl", str(command)))

    def upload(self, key: str, source: str, device_path: str) -> None:
        self._cancel_transfer.clear()
        self._commands.put(("upload", key, source, device_path))

    def download(self, key: str, device_path: str, destination: str) -> None:
        self._cancel_transfer.clear()
        self._commands.put(("download", key, device_path, destination))

    def capture(self, key: str, destination: str) -> None:
        self._cancel_transfer.clear()
        self._commands.put(("capture", key, destination))

    def cancel_transfer(self) -> None:
        self._cancel_transfer.set()

    def stop(self) -> None:
        self._closing = True
        self._cancel_transfer.set()
        self._commands.put(("stop",))
        if self.isRunning():
            # A debugger transaction may be waiting for its device timeout.
            # Do not destroy a live QThread during an ordinary window close.
            self.wait(10000)

    def run(self) -> None:
        try:
            while not self._closing:
                try:
                    command = self._commands.get(timeout=0.03)
                except queue.Empty:
                    self._poll_lines()
                    continue
                try:
                    self._dispatch(command)
                except Exception as error:  # keep the serial worker alive after an operation error
                    self.log_message.emit(str(error))
        finally:
            self._close_serial()

    def _dispatch(self, command: tuple) -> None:
        kind = command[0]
        if kind == "stop":
            self._close_serial()
        elif kind == "connect":
            try:
                self._open_serial(command[1], command[2])
            except Exception as error:
                self.connection_failed.emit(str(error))
        elif kind == "disconnect":
            self._close_serial()
        elif kind == "repl":
            if self._serial is None:
                self.repl_output.emit("Not connected to QL/32.")
                return
            command = command[1]
            if "\r" in command or "\n" in command:
                self.repl_output.emit("Enter one command at a time.")
                return
            try:
                self._serial.write(command.encode("utf-8") + b"\r")
                self._serial.flush()
            except Exception as error:
                self.repl_output.emit(f"REPL send failed: {error}")
        elif kind == "request":
            _, request_id, operation, arguments = command
            try:
                result = self._transact(request_id, operation, arguments)
                self.request_finished.emit(request_id, True, result)
            except Exception as error:
                self.request_finished.emit(request_id, False, {"error": str(error)})
        elif kind in ("upload", "download", "capture"):
            self._transfer(command)

    def _open_serial(self, port_name: str, baud: int) -> None:
        self._close_serial(emit=False)
        try:
            import serial
        except ImportError as error:
            raise RuntimeError("pyserial is required: python3 -m pip install pyserial") from error
        port = serial.Serial(port=None, baudrate=baud, timeout=0.1,
                             write_timeout=5, dsrdtr=True)
        port.port = port_name
        port.dtr = False
        port.rts = False
        port.open()
        self._serial = port
        self._line_buffer.clear()
        self._rx_buffer.clear()
        try:
            # Keep DTR unchanged on open: ESP32 USB-UART reset circuits often
            # connect it to EN, and toggling it would destroy attachable tasks.
            hello = self._transact(self._allocate_id(), "hello", ())
            self._supported = bool(hello.get("supported"))
            self.connected.emit(port_name, hello)
            if not self._supported:
                self.log_message.emit(f"Unsupported board: {hello.get('board', 'unknown')}")
        except Exception:
            self._close_serial()
            raise

    def _close_serial(self, emit: bool = True) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None
                self._supported = False
                self._line_buffer.clear()
                self._rx_buffer.clear()
                if emit:
                    self.disconnected.emit()

    def _allocate_id(self) -> int:
        with self._id_lock:
            request_id = self._next_id
            self._next_id += 1
        return request_id

    def _transact(self, request_id: int, operation: str,
                  arguments: tuple[str, ...]) -> object:
        if self._serial is None:
            raise RuntimeError("not connected")
        if operation != "hello" and not self._supported:
            raise RuntimeError("the connected board is not supported by this app")
        self._serial.write(encode_request(request_id, operation, *arguments))
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            line = self._read_line()
            if line is None:
                continue
            if not line.startswith(PREFIX.encode("ascii") + b"\t"):
                if line.strip():
                    self._emit_console_line(line)
                continue
            response_id, ok, payload = decode_response(line)
            if response_id != request_id:
                self._emit_console_line(f"Unmatched debugger reply {response_id}".encode())
                continue
            if not ok:
                raise RuntimeError(payload.get("error", "device rejected request"))
            return payload
        raise TimeoutError(f"device did not answer {operation}")

    def _read_line(self) -> bytes | None:
        if self._serial is None:
            return None
        while True:
            newline = self._rx_buffer.find(b"\n")
            if newline >= 0:
                data = self._rx_buffer[:newline]
                del self._rx_buffer[:newline + 1]
                return bytes(data).rstrip(b"\r")
            data = self._serial.read(256)
            if not data:
                return None
            self._rx_buffer.extend(data)
            while self._rx_buffer:
                byte = self._rx_buffer.pop(0)
                if byte == 10:
                    line = bytes(self._line_buffer).rstrip(b"\r")
                    self._line_buffer.clear()
                    return line
                if len(self._line_buffer) < 8192:
                    self._line_buffer.append(byte)
                else:
                    self._line_buffer.clear()

    def _poll_lines(self) -> None:
        if self._serial is None:
            return
        line = self._read_line()
        if line and not line.startswith(PREFIX.encode("ascii") + b"\t"):
            self._emit_console_line(line)

    def _emit_console_line(self, line: bytes) -> None:
        text = line.decode("utf-8", errors="replace").rstrip()
        if text:
            self.log_message.emit(text)
            self.repl_output.emit(text)

    def _transfer(self, command: tuple) -> None:
        kind, key = command[0], command[1]
        if self._serial is None:
            self.transfer_finished.emit(key, kind, "", "not connected")
            return
        if not self._supported:
            self.transfer_finished.emit(key, kind, "", "unsupported board")
            return
        if self._cancel_transfer.is_set():
            self.transfer_finished.emit(key, kind, "", "transfer cancelled")
            self._cancel_transfer.clear()
            return
        self._line_buffer.clear()
        try:
            progress = lambda done, total: self.transfer_progress.emit(key, done, total)
            status = lambda message: self.transfer_status.emit(key, message)
            cancelled = self._cancel_transfer.is_set
            if kind == "upload":
                _, _, source, device_path = command
                ql32_ymodem.send_file(self._serial, source, device_path,
                                      wait_for_prompt=False, progress=progress,
                                      cancelled=cancelled, status=status)
                result_path = device_path
            elif kind == "download":
                _, _, device_path, destination = command
                ql32_ymodem.receive_file(self._serial, destination, device_path,
                                         wait_for_prompt=False, progress=progress,
                                         cancelled=cancelled, status=status)
                result_path = destination
            else:
                _, _, destination = command
                ql32_ymodem.receive_file(self._serial, destination,
                    request_command="SCREEN", issue_command=True,
                    wait_for_prompt=False, progress=progress, cancelled=cancelled,
                    status=status)
                result_path = destination
            self.transfer_finished.emit(key, kind, result_path, "")
        except Exception as error:
            self.transfer_finished.emit(key, kind, "", str(error))
        finally:
            self._cancel_transfer.clear()
            self._line_buffer.clear()
            self._rx_buffer.clear()

