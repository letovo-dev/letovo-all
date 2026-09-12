#!/usr/bin/env python3
"""Stage one zstd Docker archive below a hard ceiling before Docker parses it."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def decompress(source, destination):
    limit = min(int(os.environ.get("CI_IMAGE_EXPANDED_LIMIT", 2 * 1024**3)), 2 * 1024**3)
    if limit < 1:
        raise ValueError("expanded image limit must be positive")
    process = None
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            process = subprocess.Popen(["zstd", "-dc", "--memory=128MB", "--", str(source)], stdout=subprocess.PIPE)
            size = 0
            while chunk := process.stdout.read(min(65536, limit - size + 1)):
                size += len(chunk)
                if size > limit:
                    raise ValueError("expanded image exceeds byte limit")
                output.write(chunk)
            if process.wait() != 0 or size == 0:
                raise ValueError("invalid or empty compressed image")
        os.replace(temporary, destination)
    finally:
        if process is not None:
            if process.poll() is None:
                process.kill()
            process.wait()
            process.stdout.close()
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: bounded_decompress.py SOURCE DESTINATION")
    decompress(Path(sys.argv[1]), Path(sys.argv[2]))
