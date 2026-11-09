# Contributor notes

Read [CONTRIBUTING.md](CONTRIBUTING.md), [docs/Coding.md](docs/Coding.md) and
[docs/ORBITALS_BUILD.md](docs/ORBITALS_BUILD.md) before changing code.

Keep changes focused. Check relevant upstream implementations and cite a ported
PR or immutable commit. Do not merge a moving upstream branch solely to repair
one issue; preserve the release baseline and review its actual diff.

Reproduce defects with production-code tests where possible. Record which tests
ran and separate unit, native GPU/kernel, and gameplay evidence. Passing a test
does not establish full compatibility or a performance improvement.

Use the same scene, settings, binary and clock policy for performance comparisons.
Keep builds and heavy diagnostics out of gameplay measurement windows. Record
cold versus warm cache behavior; distinguish first present from gameplay readiness.
Keep experiments separate from recommended builds and retain a tested rollback.

Debug facilities are opt-in. Bind the server to loopback, issue one request at
a time, and retain process/build identity. Do not upload raw memory, shader
dumps, RenderDoc captures, game assets or private configuration. See
[docs/nx2/debug-mcp.md](docs/nx2/debug-mcp.md).

Preserve saves, settings and caches. User-data maintenance needs exact scope,
a backup and explicit invocation; avoid undocumented first-run actions.

Number executable releases monotonically and tie each to its real source commit.
Keep documentation portable and preserve license/provenance notices. Do not
commit build caches, local paths or private investigation artifacts.
