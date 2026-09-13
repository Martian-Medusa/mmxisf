# Repository instructions

- This repository is a clean-room, standalone C++ implementation of the public XISF specification. Do not copy or translate code from the existing GPL-licensed `libXISF` implementation or from PixInsight/PCL.
- Treat input files as untrusted. Validate every length, offset, dimension, allocation, decompression ratio, checksum declaration, and XML limit before using it.
- Keep the core independent of PixInsight, PCL, Qt, and PFI. PFI integration belongs in PFI through a narrow adapter over the installed `mmxisf` API.
- Preserve pixel order, channel layout, sample type, numeric bounds, metadata values, and provenance exactly. Never infer unavailable metadata or silently coerce unsupported constructs.
- Fail closed on unsupported XISF versions, malformed XML, overlapping/out-of-range data blocks, arithmetic overflow, checksum failure, and resource-budget exhaustion.
- Public API changes require an ADR and SemVer impact classification. ABI stability begins only when explicitly declared before 1.0.
- Tests must cover valid fixtures, malformed inputs, boundary values, round trips where applicable, and interoperability with independently generated files. Fuzzing is part of the release gate once parsers exist.
- Astronomy binaries are Git LFS objects. Every externally sourced fixture needs origin, generator/version, license or redistribution permission, and a cryptographic hash.
- Use Conventional Commit-style subjects. Local commits are expected for coherent work; never push, publish, tag, or create a remote without explicit user approval.
