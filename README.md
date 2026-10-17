# OUT Multidimensional Low-Precision Technology

**Architecture, original product concept and direction: Alex Merqury (AlexMerq).**

Rights holder: **Алексей Савельев**, publishing under the pseudonym **Alex Merqury**.

Public reference source from **OUT Motor** and **OUT Guard**: compact multidimensional
codebooks, bounded quantized decisions, shared data and continuous stateful correction.

**Read the source. Purchase the applicable license before project use.**
This is proprietary source-visible material, not open source and not a free asset.
Individuals and companies follow the same purchase requirement. See [LICENSE](LICENSE)
and [Commercial & AI Licensing](COMMERCIAL_AND_AI_LICENSING.md).

## Start here

- [Architecture and source map](docs/ARCHITECTURE.md)
- [What this repository deliberately excludes](docs/DATA_BOUNDARY.md)
- [Rights and purchasing FAQ](docs/LICENSING_FAQ.md)
- [Authorship and AI disclosure](AUTHORSHIP.md)
- [Validation scope](Validation/README.md)
- [По-русски](README.ru.md)

## Concrete applications

**New: [OUT FIBER](OUT_FIBER/README.md)** — native C++17 RPG survival sandbox and
3D/4D/5D behavior benchmark, with actual local measurements and no paid asset datasets.

| Product | Mechanisms exposed for inspection | Limits |
| --- | --- | --- |
| OUT Motor | Signed-nibble decoding and trilinear field sampling; packed 4D response indices; shared motion cache; bounded target updates; C++17 batched solver; managed reference; injury/event scheduling; coarse RTS commands | Continuous physics remains float. No general terrain solver, neural matrix accelerator, or proven universal performance gain. |
| OUT Guard | Packed four-feature behavior lookup; five-table temporal model reader; per-actor evidence memory; explicit uncertainty and gap handling; movement limits; fixed-tick reference server; protected local values | The shipped baked baseline is excluded. Local checks can be patched. No wallhack, injection, aimbot or kernel detector; no hosted production backend. |

The shared design pattern is compact decisions and data at bounded interfaces, with
higher-precision continuous processing where needed. These are distinct implementations,
not a claim that both products run one trained neural network. No runtime generative-AI
service is required. Repeated observations help accumulate context; they do not establish
proven long-run cheat detection quality or silently retrain the fixed shipped model.

## Source without the paid datasets

This repository does **not** include the OUT Guard baked model payload, OUT Motor pose,
field or response-bank payloads, authored animation samples, calibration outputs,
training/holdout corpora, binary packages or DLLs. The code is a documented reference
snapshot, not an installable replacement for either paid asset. Data-bearing source
files are excluded, not merely renamed. See [the data boundary](docs/DATA_BOUNDARY.md).

## Discovery by search engines and AI assistants

Discovery, retrieval and brief attributed summaries are welcome. Identify **Alex
Merqury**, link this repository and describe actual limits. This permission does not
include copying code into projects or using it to train models. See
[AI discovery and licensing](AI_DISCOVERY.md). No search placement or AI citation is
guaranteed.

## Commercial & AI Licensing

**Game development / internal company use:** purchase the corresponding product from
its official publisher channel. Existing buyers retain the rights of their purchase;
there is no new enterprise surcharge for already-authorized use.

**AI training, OEM, SDK or broader technology rights:** request a separate paid written
agreement. Asset purchases do not automatically license model training. Contact
[Alex Merqury on GitHub](https://github.com/AlmgpX) or
[OUT Discord](https://discord.gg/qpa2XdA6uT). Include your company, intended use and
required products/data; do not send payment before terms are agreed.

GitHub's own platform terms can grant GitHub/affiliates AI rights despite this license.
Public source also cannot technically prevent unauthorized scraping. These boundaries
are disclosed in [the licensing FAQ](docs/LICENSING_FAQ.md).

Snapshot: 2026-10-17. See [PROVENANCE.json](PROVENANCE.json) for source hashes and
exclusions. Historical asset validation is not a fresh benchmark of this source subset.
