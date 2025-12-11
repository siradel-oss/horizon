import tarfile
from pathlib import Path
import sys

tar = tarfile.open(sys.argv[1])
output = Path(sys.argv[2])
if not output.exists():
    output.mkdir(parents=True)
tar.extractall(path=output)
tar.close()
