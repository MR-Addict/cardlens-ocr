import ctypes as c
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'sdk/python'))
from cardlens_ocr import ocr

class SdkTests(unittest.TestCase):
    def tearDown(self):
        ocr.release()

    def test_lifecycle_and_errors(self):
        with self.assertRaisesRegex(RuntimeError, 'NOT_INITIALIZED'):
            ocr.recognize(b'', {})
        with self.assertRaisesRegex(RuntimeError, 'MODEL_LOAD_FAILED'):
            ocr.initialize(ROOT / 'out/missing-models')
        for _ in range(2):
            ocr.initialize(ROOT / 'models')
            with self.assertRaisesRegex(RuntimeError, 'ALREADY_INITIALIZED'):
                ocr.initialize(ROOT / 'models')
            with self.assertRaisesRegex(ValueError, 'INVALID_IMAGE'):
                ocr.recognize(b'broken', {'documentType': 'id_card', 'side': 'portrait'})
            ocr.release()
            ocr.release()

    def test_reference_fields(self):
        ocr.initialize(ROOT / 'models')
        for case in json.loads((ROOT / 'tests/gold.json').read_text(encoding='utf-8'))['cases']:
            result = ocr.recognize(ROOT / 'out/testdata' / case['file'],
                {'documentType': case['type'], 'side': case['side']})
            for name, value in case['expected'].items():
                self.assertEqual(result['fields'][name]['value'], value, (case['file'], name))

    def test_c_boundary_checks(self):
        ocr.initialize(ROOT / 'models')
        lib = ocr._lib
        for handle, length, width, height, stride in [
            (None, 0, 800, 800, 3200), (ocr._handle, 10, 800, 800, 3200),
            (ocr._handle, 10, 800, 800, 2**63), (ocr._handle, 10, -1, 800, 3200)]:
            result, error = c.c_void_p(), c.c_void_p()
            code = lib.cl_recognize(handle, b'0123456789', length, width, height, stride,
                b'id_card', b'portrait', c.byref(result), c.byref(error))
            self.assertNotEqual(code, 0)
            self.assertFalse(result)
            self.assertTrue(error)
            lib.cl_free(error)

if __name__ == '__main__':
    unittest.main()
