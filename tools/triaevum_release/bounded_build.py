"""Bounded compilation scheduling with no new work after an observed failure."""

from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait


def run_bounded(worker, items, jobs: int) -> None:
    if jobs < 1:
        raise ValueError("Build worker count must be positive")
    remaining = iter(items)
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        pending = set()
        def refill():
            while len(pending) < jobs:
                try:
                    item = next(remaining)
                except StopIteration:
                    return
                pending.add(executor.submit(worker, item))
        refill()
        try:
            while pending:
                completed, pending = wait(pending, return_when=FIRST_COMPLETED)
                # Check the entire completed batch before starting replacements.
                for future in completed:
                    future.result()
                refill()
        finally:
            for future in pending:
                future.cancel()
