from __future__ import annotations

import unittest

from tools.ql32_remote_debugger.protocol import encode_response
from tools.ql32_remote_debugger.serial_worker import SerialWorker


class SimulatedSerial:
    def __init__(self):
        self.incoming = bytearray()
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))
        fields = bytes(data).rstrip(b"\r").split(b"\t")
        request_id = int(fields[1])
        operation = fields[2].decode("ascii")
        if operation == "hello":
            self.incoming.extend(b"board startup\r\n")
            self.incoming.extend(encode_response(request_id, True, {
                "version": 1,
                "board": "ESP32-2432S028",
                "supported": True,
            }).encode("utf-8"))
        else:
            self.incoming.extend(encode_response(request_id, True, {"ok": True}).encode())
        return len(data)

    def read(self, count):
        result = self.incoming[:count]
        del self.incoming[:count]
        return bytes(result)


class SerialWorkerTests(unittest.TestCase):
    def test_transaction_skips_monitor_lines_and_matches_request_id(self):
        worker = SerialWorker()
        port = SimulatedSerial()
        worker._serial = port
        worker.log_message.connect(lambda _message: None)

        result = worker._transact(12, "hello", ())

        self.assertEqual(result["board"], "ESP32-2432S028")
        self.assertEqual(len(port.writes), 1)
        self.assertTrue(port.writes[0].startswith(b"@QL32DBG/1\t12\thello\r"))
        worker._serial = None

    def test_non_hello_request_is_rejected_for_unsupported_board(self):
        worker = SerialWorker()
        worker._serial = SimulatedSerial()
        with self.assertRaisesRegex(RuntimeError, "not supported"):
            worker._transact(13, "tasks.list", ())
        self.assertEqual(worker._serial.writes, [])
        worker._serial = None

    def test_transfer_cancel_requested_before_dispatch_is_not_lost(self):
        worker = SerialWorker()
        port = SimulatedSerial()
        worker._serial = port
        worker._supported = True
        finished = []
        worker.transfer_finished.connect(lambda *result: finished.append(result))
        worker.cancel_transfer()

        worker._transfer(("upload", "job", "/unused/file.bas", "SDC:/file.bas"))

        self.assertEqual(port.writes, [])
        self.assertEqual(finished, [("job", "upload", "", "transfer cancelled")])
        worker._serial = None


if __name__ == "__main__":
    unittest.main()
