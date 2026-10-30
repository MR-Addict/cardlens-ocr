import sys
from cardlens_ocr import ocr

ocr.initialize('models')
try:
    print(ocr.recognize(sys.argv[1], {'documentType': 'id_card', 'side': 'portrait'}))
finally:
    ocr.release()
