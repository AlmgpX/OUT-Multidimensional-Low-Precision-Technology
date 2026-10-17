# Public source / paid data boundary

## Not published

- OUT Guard `GuardBuiltInTemporalModel.cs`, including its embedded base64 model payload.
- OUT Motor `OUTMotorData.cs`, including pose, vector-field, response-bank and gain data.
- All authored animation banks, `.asset` files and private Mixamo-derived samples.
- Training, calibration and held-out sequence corpora; generated candidate profiles.
- Model/pose/response binary exports, DLLs, packages, release ZIPs and historical git objects.
- Model baking tools/corpus generators, private source assets and customer telemetry.

No Git history from the product repository is copied: otherwise deleted data could
remain accessible in previous commits. A new history starts with this reviewed subset.
The payload exclusion audit checks tracked files, long encoded literals, prohibited
file types, and known withheld payload signatures before publication.

## Published

Implementation logic, type/format definitions, deterministic heuristic formulas,
small hand-written regression fixtures, architecture documentation and source hashes.
A generic loader or a format constant is not the actual dataset. The decoder excerpt
has no pose/field/response arrays and is explicitly not a complete compilation unit.

The public code may be read to understand algorithms; it remains subject to LICENSE.
The paid assets remain separate complete deliveries. Do not add datasets to issues,
pull requests, Git LFS, releases, artifacts or future repository history.

This boundary applies to this new repository. It does not erase or change content
already present in any other repository, download, cache or previously shared archive.
