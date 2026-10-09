# Contributing to GLCAD

Contributions are welcome: bug reports, drawings that fail to open, fixes,
tests and features.

GLCAD is source-available under the [GLCAD Noncommercial Source License](LICENSE).
It is not an open-source license. You may download, build, use and modify the
source for noncommercial purposes. Commercial use and commercialization are
covered by [COMMERCIAL-LICENSE.md](COMMERCIAL-LICENSE.md).

## Reporting problems

Open a GitHub issue with the platform, OS version, GLCAD version and the steps
to reproduce. If a file triggers the problem, attach it only if you have the
right to share it publicly; otherwise describe it or reduce it to a minimal
drawing. Report security problems privately as described in [SECURITY.md](SECURITY.md).

## Pull requests

1. Fork the repository and branch from `main`.
2. Build and run the fast test suite:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build -L fast
   ```

3. Match the surrounding code style. Add or update tests for behavior you change.
4. Keep each pull request focused on one change and describe what it does and
   how you verified it.
5. Do not add third-party code, fonts, drawings or other assets unless their
   license permits redistribution in this repository and in the official
   application. Name the source and license in the pull request.

## Contributor terms

The official GLCAD application is sold through the App Store, and the licensor,
Geramy Loveless, reserves commercial rights in the project. So that
contributions can be included in every edition, opening a pull request or
otherwise submitting a contribution means you agree to the following:

- You wrote the contribution, or otherwise have the right to submit it under
  these terms.
- You keep the copyright in your contribution.
- You grant Geramy Loveless and his successors a perpetual, worldwide,
  non-exclusive, royalty-free, irrevocable license to use, copy, modify,
  distribute, sublicense and commercially exploit your contribution, as part of
  GLCAD or a product derived from it, under any license terms, including the
  GLCAD Noncommercial Source License and paid commercial terms.
- You grant a patent license of the same scope for any of your patent claims
  that the contribution necessarily infringes, alone or combined with GLCAD.
- Everyone else receives your contribution under the GLCAD Noncommercial Source
  License.
- Contributions are provided as-is, without warranty, and you are not required
  to provide support.

If you cannot agree to these terms, for example because an employer owns your
work, do not submit the contribution. Open an issue to discuss it instead.
