# DX7 factory-style banks (tolerated, not legally cleared)

This directory is **intentionally empty in the repository**. It is populated
only when a build opts in via `-DYSE_FETCH_CONTENT_PACK=ON`, which pulls the
`.syx` banks listed under the `dx7-factory` rows of
[`../../pack-manifest.cmake`](../../pack-manifest.cmake).

## Legal status

The DX7 factory-style ROM voice banks are **tolerated, not legally cleared**
(maintainer decision, taken in the scoping of issue #149 / #179). Yamaha has
never released these voice data sets under an open license; they are bundled
here as a deliberate, documented risk because they are the de-facto reference
material for the FM importer (#177) and are distributed widely.

The risk is **reversible by deleting this one folder**: nothing else in the
content pack, the engine, or the build depends on its contents. The importer
(`YseEngine/dsp/fm/dx7Sysex.cpp`) and the smoke test both treat this directory
as optional and skip gracefully when it is absent.

For a clean, unambiguously CC0 FM bank authored by this project, see
[`../original/yse_originals.syx`](../original/yse_originals.syx).

Every asset that lands here is recorded, with its tolerated-unclear status, in
the repo-root [`CONTENT-LICENSES.md`](../../../CONTENT-LICENSES.md).
