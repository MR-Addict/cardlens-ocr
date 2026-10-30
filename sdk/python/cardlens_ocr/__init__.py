"""Offline document recognition: initialize, recognize, release."""
import ctypes as c
import io
import json
from pathlib import Path
import platform
import threading
from PIL import Image, ImageOps

class _Ocr:
    def __init__(self):
        self._lock = threading.Lock()
        self._handle = c.c_void_p()
        self._lib = None

    def initialize(self, model_directory, *, library=None):
        with self._lock:
            if self._handle:
                raise RuntimeError('ALREADY_INITIALIZED')
            if library is None:
                root = Path(__file__).resolve().parents[3]
                name = {'Linux': 'libcardlens.so', 'Darwin': 'libcardlens.dylib', 'Windows': 'cardlens.dll'}[platform.system()]
                directories = [Path.cwd(), root]
                candidates = [directory / 'out/build/native' / ('Release' if platform.system() == 'Windows' else '') / name for directory in directories]
                library = next((path for path in candidates if path.is_file()), candidates[0])
            lib = c.CDLL(str(library))
            lib.cl_initialize.argtypes = [c.c_char_p, c.POINTER(c.c_void_p), c.POINTER(c.c_void_p)]
            lib.cl_initialize.restype = c.c_int
            lib.cl_recognize.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_int, c.c_int,
                                        c.c_size_t, c.c_char_p, c.c_char_p,
                                        c.POINTER(c.c_void_p), c.POINTER(c.c_void_p)]
            lib.cl_recognize.restype = c.c_int
            lib.cl_free.argtypes = [c.c_void_p]
            lib.cl_free.restype = None
            lib.cl_release.argtypes = [c.c_void_p]
            lib.cl_release.restype = None
            error = c.c_void_p()
            if lib.cl_initialize(str(Path(model_directory).resolve()).encode(), c.byref(self._handle), c.byref(error)):
                self._raise(lib, error)
            self._lib = lib

    @staticmethod
    def _raise(lib, error):
        message = c.string_at(error).decode() if error else 'OCR_FAILED'
        lib.cl_free(error)
        raise RuntimeError(message)

    def recognize(self, image, options):
        with self._lock:
            if not self._handle:
                raise RuntimeError('NOT_INITIALIZED')
            try:
                source = Image.open(io.BytesIO(image) if isinstance(image, (bytes, bytearray)) else image)
                with source:
                    if source.format not in ('JPEG', 'PNG'):
                        raise ValueError('UNSUPPORTED_IMAGE_FORMAT')
                    width, height = source.size
                    if min(width, height) < 64 or max(width, height) > 8192 or width * height > 24000000:
                        raise ValueError('INVALID_IMAGE')
                    rgba = ImageOps.exif_transpose(source).convert('RGBA')
                    width, height = rgba.size
                    data = rgba.tobytes()
            except (OSError, Image.DecompressionBombError) as error:
                raise ValueError('INVALID_IMAGE') from error
            result, error = c.c_void_p(), c.c_void_p()
            code = self._lib.cl_recognize(self._handle, data, len(data), width, height, width * 4,
                options['documentType'].encode(), options['side'].encode(), c.byref(result), c.byref(error))
            if code:
                self._raise(self._lib, error)
            try:
                return json.loads(c.string_at(result).decode())
            finally:
                self._lib.cl_free(result)

    def release(self):
        with self._lock:
            if self._handle:
                self._lib.cl_release(self._handle)
                self._handle = c.c_void_p()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.release()

ocr = _Ocr()
__all__ = ['ocr']
