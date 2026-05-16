# bench-history

This branch stores the historic results of the libYSE benchmark suite.

It is **not** a source branch — do not merge it into `master` or `dev`. The
`.github/workflows/benchmark.yml` workflow on `master` / `dev` pushes
benchmark JSON to this branch via
[`benchmark-action/github-action-benchmark`](https://github.com/benchmark-action/github-action-benchmark),
which uses the contents to:

1. Compare new runs against the most recent baseline and surface
   regressions past the configured threshold as PR comments.
2. Maintain a historic chart on the GitHub Pages site served from this
   branch (if Pages is configured to publish from `bench-history`).

The branch starts as an orphan — it has no shared history with `master` —
so cloning it does not pull in the engine source. Its content is managed
entirely by the action.

## Reading the data

The action writes `data.js` at the branch root containing the raw history
plus an HTML chart wrapper. Open `index.html` in a browser to see the
trend over time, or read `data.js` directly for the underlying JSON.

## Maintenance

If the history accumulates more entries than you want to keep, the action
honours a `max-items-in-chart` parameter — bump it in `benchmark.yml`. To
reset entirely, delete this branch and re-run the workflow; the action
recreates it on first push.
