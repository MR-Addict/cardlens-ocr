import hashlib
import http.client
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, MagicMock
import urllib.error

from fetch_references import fetch_reference


class FetchReferenceTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / 'out')
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name)
        self.data = b'reference image'
        self.row = {'id': 'sample', 'url': 'https://example.com/image.jpg',
                    'sha256': hashlib.sha256(self.data).hexdigest()}
        self.target = self.path / 'sample.jpg'
        self.response = MagicMock()
        self.response.__enter__.return_value.read.return_value = self.data

    def test_transient_failures_retry_before_atomic_write(self):
        failures = [urllib.error.URLError(TimeoutError('TLS handshake timed out')),
                    http.client.IncompleteRead(b'partial'), self.response]
        with patch('fetch_references.urllib.request.urlopen', side_effect=failures) as request, \
                patch('fetch_references.time.sleep') as sleep:
            fetch_reference(self.row, self.path)
        self.assertEqual(request.call_count, 3)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [2, 4])
        self.assertEqual(self.target.read_bytes(), self.data)
        self.assertEqual(list(self.path.iterdir()), [self.target])

    def test_valid_cache_needs_no_network_and_corrupt_cache_is_repaired(self):
        self.target.write_bytes(self.data)
        with patch('fetch_references.urllib.request.urlopen') as request:
            fetch_reference(self.row, self.path)
            request.assert_not_called()
        self.target.write_bytes(b'corrupt')
        with patch('fetch_references.urllib.request.urlopen', return_value=self.response):
            fetch_reference(self.row, self.path)
        self.assertEqual(self.target.read_bytes(), self.data)

    def test_retry_limit_does_not_leave_a_partial_file(self):
        with patch('fetch_references.urllib.request.urlopen', side_effect=TimeoutError('timeout')) as request, \
                patch('fetch_references.time.sleep'):
            with self.assertRaisesRegex(RuntimeError, 'sample: download failed after 4 attempts'):
                fetch_reference(self.row, self.path)
        self.assertEqual(request.call_count, 4)
        self.assertEqual(list(self.path.iterdir()), [])

    def test_changed_upstream_and_permanent_http_errors_fail_immediately(self):
        self.response.__enter__.return_value.read.return_value = b'changed upstream'
        errors = [None, urllib.error.HTTPError(self.row['url'], 404, 'missing', {}, None)]
        for error in errors:
            with self.subTest(error=error), \
                    patch('fetch_references.urllib.request.urlopen', return_value=self.response, side_effect=error) as request, \
                    patch('fetch_references.time.sleep') as sleep:
                with self.assertRaises((RuntimeError, urllib.error.HTTPError)):
                    fetch_reference(self.row, self.path)
                self.assertEqual(request.call_count, 1)
                sleep.assert_not_called()
                self.assertFalse(self.target.exists())


if __name__ == '__main__':
    unittest.main()
