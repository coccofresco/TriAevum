"""Bounded Windows sharing-lock tolerance for completed private staging trees."""

import time
from pathlib import Path


def publish_directory(source: Path, destination: Path) -> None:
    for attempt in range(6):
        if destination.exists():
            raise FileExistsError("Refusing to replace a published directory: " + str(destination))
        try:
            source.rename(destination)
            return
        except OSError as error:
            if getattr(error, "winerror", None) not in (5, 32, 33) or attempt == 5:
                raise
            time.sleep(0.1 * (attempt + 1))
