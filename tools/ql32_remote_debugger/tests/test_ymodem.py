from __future__ import annotations

import tempfile
import unittest
from collections import deque
from pathlib import Path

from tools import ql32_ymodem as ymodem


def _frame(marker: int, sequence: int, payload: bytes) -> bytes:
    crc = ymodem.crc16(payload)
    return (bytes((marker, sequence, 0xFF - sequence)) + payload +
            bytes((crc >> 8, crc & 0xFF)))


class SimulatedDevice:
    """Small serial peer that speaks the monitor's YMODEM batch exchange."""

    def __init__(self, *, retry_first_data=False, cancel_upload_data=False):
        self.incoming = deque()
        self.outgoing = bytearray()
        self.command = None
        self.phase = ""
        self.data_packets = 0
        self.retry_first_data = retry_first_data
        self.cancel_upload_data = cancel_upload_data
        self.commands = []
        self.cancelled_by_host = False

    def read(self, count=1):
        result = bytearray()
        while self.incoming and len(result) < count:
            result.append(self.incoming.popleft())
        return bytes(result)

    def write(self, data):
        data = bytes(data)
        if self.phase == "" and (data.startswith(b"RECV ") or data.startswith(b"SEND ") or data == b"SCREEN\r"):
            text = data.decode("ascii").strip()
            self.commands.append(text)
            self.command = text.split()[0]
            self.incoming.extend(f"YMODEM READY {self.command}\r\n".encode())
            if self.command == "RECV":
                self.incoming.append(ymodem.CRC_REQUEST)
                self.phase = "upload_header"
            else:
                self.phase = "download_start"
            return len(data)

        if self.phase == "download_start" and data == bytes((ymodem.CRC_REQUEST,)):
            payload = ymodem.make_header("device.bas", 5)
            self.incoming.extend(_frame(ymodem.SOH, 0, payload))
            self.phase = "download_header"
            return len(data)

        self.outgoing.extend(data)
        self._process_upload()
        self._process_download()
        return len(data)

    def _process_upload(self):
        while self.phase.startswith("upload"):
            if self.phase == "upload_header" and len(self.outgoing) >= 133:
                packet = self.outgoing[:133]
                del self.outgoing[:133]
                self.incoming.extend(bytes((ymodem.ACK, ymodem.CRC_REQUEST)))
                self.phase = "upload_data"
            elif self.phase == "upload_data" and self.outgoing and self.outgoing[0] in (ymodem.SOH, ymodem.STX):
                marker = self.outgoing[0]
                size = 133 if marker == ymodem.SOH else 1029
                if len(self.outgoing) < size:
                    return
                del self.outgoing[:size]
                self.data_packets += 1
                if self.cancel_upload_data:
                    self.incoming.extend(bytes((ymodem.CAN, ymodem.CAN)))
                    self.incoming.extend(
                        b"Transfer failed: YMODEM receive timed out\r\n> ")
                    self.phase = ""
                    return
                response = ymodem.NAK if self.retry_first_data and self.data_packets == 1 else ymodem.ACK
                self.incoming.append(response)
            elif self.phase == "upload_data" and self.outgoing[:1] == bytes((ymodem.EOT,)):
                del self.outgoing[:1]
                self.incoming.append(ymodem.NAK)
                self.phase = "upload_eot_second"
            elif self.phase == "upload_eot_second" and self.outgoing[:1] == bytes((ymodem.EOT,)):
                del self.outgoing[:1]
                self.incoming.extend(bytes((ymodem.ACK, ymodem.CRC_REQUEST)))
                self.phase = "upload_batch"
            elif self.phase == "upload_batch" and len(self.outgoing) >= 133:
                del self.outgoing[:133]
                self.incoming.append(ymodem.ACK)
                self.phase = ""
            elif len(self.outgoing) >= 2 and self.outgoing[:2] == bytes((ymodem.CAN, ymodem.CAN)):
                self.outgoing.clear()
                self.cancelled_by_host = True
                self.phase = ""
                return
            else:
                return

    def _process_download(self):
        if self.phase == "download_header" and self.outgoing.endswith(bytes((ymodem.ACK, ymodem.CRC_REQUEST))):
            self.outgoing.clear()
            self.incoming.extend(_frame(ymodem.STX, 1, b"HELLO".ljust(1024, b"\x1a")))
            self.phase = "download_data"
        elif self.phase == "download_data" and self.outgoing.endswith(bytes((ymodem.ACK,))):
            self.outgoing.clear()
            self.incoming.append(ymodem.EOT)
            self.phase = "download_eot1"
        elif self.phase == "download_eot1" and self.outgoing.endswith(bytes((ymodem.NAK,))):
            self.outgoing.clear()
            self.incoming.append(ymodem.EOT)
            self.phase = "download_eot2"
        elif self.phase == "download_eot2" and self.outgoing.endswith(bytes((ymodem.ACK, ymodem.CRC_REQUEST))):
            self.outgoing.clear()
            self.incoming.extend(_frame(ymodem.SOH, 0, bytes(128)))
            self.phase = "download_batch"
        elif self.phase == "download_batch" and self.outgoing.endswith(bytes((ymodem.ACK,))):
            self.outgoing.clear()
            self.phase = ""


class YModemTests(unittest.TestCase):
    def test_upload_retries_and_completes(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "program.bas"
            source.write_bytes(b"PRINT 42\n")
            device = SimulatedDevice(retry_first_data=True)
            ymodem.send_file(device, source, "FFS:/program.bas", wait_for_prompt=False,
                             progress=lambda *_: None)
            self.assertEqual(device.commands, ['RECV "FFS:/program.bas"'])
            self.assertEqual(device.data_packets, 2)

    def test_upload_reports_waiting_for_device_destination_setup(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "program.bas"
            source.write_bytes(b"PRINT 42\n")
            device = SimulatedDevice()
            statuses = []
            ymodem.send_file(device, source, "FFS:/program.bas",
                             wait_for_prompt=False, status=statuses.append)
            self.assertIn("Waiting for QL/32 to prepare the destination", statuses)
            self.assertIn("Sending file data", statuses)

    def test_upload_reports_receiver_error_after_cancel(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "program.bas"
            source.write_bytes(b"PRINT 42\n")
            device = SimulatedDevice(cancel_upload_data=True)
            with self.assertRaisesRegex(
                    RuntimeError,
                    r"transfer cancelled by device: Transfer failed: YMODEM receive timed out"):
                ymodem.send_file(device, source, "FFS:/program.bas",
                                 wait_for_prompt=False, progress=lambda *_: None)

    def test_download_and_screen_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            device = SimulatedDevice()
            destination = Path(directory) / "program.bas"
            ymodem.receive_file(device, destination, "FFS:/program.bas",
                                wait_for_prompt=False, progress=lambda *_: None)
            self.assertEqual(destination.read_bytes(), b"HELLO")

            capture_device = SimulatedDevice()
            capture = Path(directory) / "screen.bmp"
            ymodem.receive_file(capture_device, capture, request_command="SCREEN",
                                issue_command=True, wait_for_prompt=False,
                                progress=lambda *_: None)
            self.assertEqual(capture_device.commands, ["SCREEN"])
            self.assertEqual(capture.read_bytes(), b"HELLO")

    def test_upload_cancellation_sends_cancel_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "large.bas"
            source.write_bytes(b"x" * 2048)
            device = SimulatedDevice()
            cancelled = False

            def progress(*_):
                nonlocal cancelled
                cancelled = True

            with self.assertRaises(ymodem.TransferCancelled):
                ymodem.send_file(device, source, "FFS:/large.bas", wait_for_prompt=False,
                                 progress=progress, cancelled=lambda: cancelled)
            self.assertTrue(device.cancelled_by_host)

    def test_cancellation_interrupts_wait_for_monitor_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "program.bas"
            source.write_bytes(b"PRINT 1\n")
            device = SimulatedDevice()
            with self.assertRaises(ymodem.TransferCancelled):
                ymodem.send_file(device, source, "FFS:/program.bas",
                                 wait_for_prompt=False, cancelled=lambda: True)
            self.assertEqual(device.commands, ['RECV "FFS:/program.bas"'])
            self.assertTrue(device.cancelled_by_host)


if __name__ == "__main__":
    unittest.main()
