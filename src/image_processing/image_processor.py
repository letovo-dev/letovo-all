import ctypes
import sys

lib = ctypes.CDLL('./libimage.so')

lib.process_image.argtypes = [ctypes.c_char_p] 
lib.process_image.restype = None

def process_image(filename):
    if isinstance(filename, str):
        filename = filename.encode('utf-8')
    lib.process_image(filename)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python image_processor.py <input_image.png>")
    else:
        process_image(sys.argv[1])