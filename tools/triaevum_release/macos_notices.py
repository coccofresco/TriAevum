"""Reviewed macOS donor notices. Other platforms retain their existing policy."""

import hashlib


LICENSE_SHA256 = {
    "LICENSES/MoltenVK-Apache-2.0.txt": "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
    "LICENSES/Vulkan-Loader-LICENSE.txt": "43c0a37e6a0fa7ff3c843b3ec5a4fac84b712558ddac103fbd4c1649662a9ece",
    "LICENSES/MoltenVK-SPIRV-Cross-LICENSE.txt": "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
    "LICENSES/MoltenVK-SPIRV-Tools-LICENSE.txt": "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30",
    "LICENSES/MoltenVK-SPIRV-Headers-LICENSE.txt": "ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613",
    "LICENSES/MoltenVK-Vulkan-Headers-LICENSE.txt": "95ad366d23fadf701d355bc45fb8b82ae2d700239471d35d41286ac3b08ff903",
    "LICENSES/MoltenVK-cereal-LICENSE.txt": "58604a126af6a671ea390ee3d5b3e42228aa59b2858fb7af1d5b20b31739ccbb",
    "LICENSES/Vulkan-Loader-source-NOTICES.txt": "a95b0f4c29be7af48a094e6a9653210ec2842df1285da67db280b26551c33266",
}
PROVENANCE = "LICENSES/MacOS-donor-notices.json"


def notice_errors(read_bytes):
    """Accept a source-archive or package reader; never accept placeholder text."""
    for name, expected in LICENSE_SHA256.items():
        try:
            actual = hashlib.sha256(read_bytes(name)).hexdigest()
        except (OSError, KeyError):
            yield "Missing macOS donor notice: " + name
            continue
        if actual != expected:
            yield "Unreviewed macOS donor notice: " + name


def verified_notices(documents, moltenvk_license, vulkan_license):
    errors = list(notice_errors(documents.__getitem__))
    if errors:
        raise ValueError("\n".join(errors))
    for name, supplied in (("LICENSES/MoltenVK-Apache-2.0.txt", moltenvk_license),
                           ("LICENSES/Vulkan-Loader-LICENSE.txt", vulkan_license)):
        if supplied.read_bytes() != documents[name]:
            raise ValueError("Dependency notice differs from verified corresponding source: " + name)
    return {name: documents[name] for name in (*LICENSE_SHA256, PROVENANCE)}
