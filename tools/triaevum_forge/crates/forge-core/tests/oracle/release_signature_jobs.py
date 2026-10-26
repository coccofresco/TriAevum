"""Oracle jobs for python_oracle_signature.rs: the release tooling's Ed25519
signatures (tools/triaevum_release/release_signature.py).

    release_signature_jobs.py RELEASE_TOOLS sign MANIFEST KEY_PATH   -> {"entry", "signature"}
    release_signature_jobs.py RELEASE_TOOLS verify MANIFEST SIGNATURE KEY_ID PUBLIC_HEX -> {"errors"}
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
import release_signature  # noqa: E402

command = sys.argv[2]
manifest = Path(sys.argv[3]).read_bytes()
if command == "sign":
    key = Path(sys.argv[4])
    entry = release_signature.generate_key(key, "oracle")
    print(json.dumps({"entry": entry, "signature": release_signature.sign_manifest(manifest, key)}))
elif command == "verify":
    signature = Path(sys.argv[4]).read_bytes()
    keys = {sys.argv[5]: bytes.fromhex(sys.argv[6])}
    print(json.dumps({"errors": release_signature.signature_errors(manifest, signature, keys)}))
else:
    raise SystemExit(f"unknown job {command}")
