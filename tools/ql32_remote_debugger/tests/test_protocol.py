import unittest

from tools.ql32_remote_debugger.protocol import (
    decode_argument,
    decode_response,
    encode_request,
    encode_response,
)


class ProtocolCodecTests(unittest.TestCase):
    def test_request_encodes_tabs_and_unicode(self):
        request = encode_request(17, "debug.eval", "2+3", "path with\tseparator/č.bas")
        self.assertIn(b"%09", request)
        self.assertIn(b"%C4%8D", request)
        self.assertEqual(decode_argument("path%20with%09separator%2F%C4%8D.bas"),
                         "path with\tseparator/č.bas")

    def test_response_round_trip_and_error(self):
        original = {"line": 12, "text": 'quoted "value"\n'}
        line = encode_response(5, True, original)
        self.assertEqual(decode_response(line), (5, True, original))
        self.assertEqual(decode_response(encode_response(6, False, {"error": "bad request"})),
                         (6, False, {"error": "bad request"}))

    def test_rejects_unframed_and_malformed_responses(self):
        for value in (b"hello", b"@QL32DBG/1\t1\tOK", b"@QL32DBG/1\tno\tOK\t{}"): 
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_response(value)


if __name__ == "__main__":
    unittest.main()
