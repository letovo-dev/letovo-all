import ctypes
import sys

# Load the compiled shared library
lib = ctypes.CDLL('./libimage.so')  # Adjust the path for your OS

# Specify the function signature
lib.process_image.argtypes = [ctypes.c_char_p]  # Argument: const char*
lib.process_image.restype = None                # Return type: void

def process_image(filename):
    """Python wrapper for the C process_image function."""
    # Ensure the filename is passed as bytes (required by ctypes)
    if isinstance(filename, str):
        filename = filename.encode('utf-8')
    lib.process_image(filename)

# Example usage
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python image_processor.py <input_image.png>")
    else:
        process_image(sys.argv[1])