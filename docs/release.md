# Making a release

A HeyListen release holds the **HeyListen Builder** and nothing of the game: the Windows setup (`HeyListenBuilder.exe`) and
its list (`heylisten-setup.txt`), the Linux AppImage and its list (`heylisten-linux.txt`), the user guide and
`SHA256SUMS.txt`. The builder's update parts live on two releases of their own, `builder-parts` (Windows) and `linux-parts`
(Linux), named by their checksum, so a part that did not change is never uploaded twice.

Releases are made on GitHub's runners by [`.github/workflows/release.yml`](../.github/workflows/release.yml), from the tag
and nothing else, and end as a **draft**: the maintainer reads it and publishes it by hand. The Windows files are signed by
[SignPath Foundation](https://signpath.org) once its project is set up (below); until then the same workflow makes the same
files unsigned and says so in a notice.

## The steps

1. Write `docs/release/RELEASE_NOTES.md` (its `{TAG}` becomes the tag) and merge everything into `main`.
2. Tag and push: `git tag v0.4.1 && git push origin v0.4.1`. That starts the workflow:

   | Job | Runner | What it does |
   |---|---|---|
   | Version and mode | Ubuntu | Reads the tag; a draft and release-signing only for a tag's own code. |
   | Windows setup and builder | Windows | `make_release.py --stage build`: the setup (`setup.c`, llvm-mingw) and the builder (`dotnet publish`). Uploads the three files we build and sign as the artifact `windows-unsigned`. No cache. |
   | Code signing (SignPath) | Ubuntu | Sends `windows-unsigned` to SignPath ([artifact configuration](../.signpath/artifact-configurations/heylisten-builder.xml)), waits for the signature (release-signing waits for an approver), uploads `windows-signed`. Skipped with a notice while SignPath is not set up. |
   | Windows release files | Windows | Checks the signatures, then `make_release.py --stage package --signed ...`: the parts and `heylisten-setup.txt` from the **signed** files, the notes, the FORBIDDEN check. |
   | Linux | Ubuntu 22.04 container | [`linux.yml`](../.github/workflows/linux.yml), called: the game-free checks and the AppImage with its parts. Publishes nothing when called. |
   | Draft release | Ubuntu | `make_release_linux.sh` (the Linux gate), `collect_release.py` (FORBIDDEN over every file and zip again, the Windows list against its parts, `SHA256SUMS.txt` over every asset and new part, computed after signing), then the draft. |

3. If SignPath is set up, approve the signing request in SignPath (the job waits for it: `SIGNPATH_WAIT_SECONDS`, two hours
   by default; after that the job fails and can be run again).
4. Upload the new parts, unless `RELEASE_UPLOAD_PARTS` does it (below). The run's `release-files` artifact holds them:

   ```sh
   gh run download <run id> -R Sacrappy/HeyListen -n release-files -D release-files
   gh release upload builder-parts -R Sacrappy/HeyListen release-files/builder-parts/*.zip   # (skip names already there)
   GITHUB_REPOSITORY=Sacrappy/HeyListen GITHUB_SHA=$(git rev-parse v0.4.1) GH_TOKEN=$(gh auth token) \
     engine/scripts/linux/publish_parts.sh "$PWD/release-files/linux-parts"
   ```

5. Read the draft (Releases page): the files, the notes, a test install from `HeyListenBuilder.exe` and the AppImage. Then
   **publish it by hand**. Publishing does not start anything else: `linux.yml` sees that the release already has
   `heylisten-linux.txt` and does nothing.

Nothing in the workflow publishes a release, pushes a tag or posts anything. An existing release or draft for the tag is
never replaced: delete the draft by hand to make it again (re-run the run's failed jobs, or run the workflow from the tag
with `draft` on, below).

`tools/release/make_release.py` (Windows) still makes the Windows files on a PC, unsigned, in one go (`--stage all`, the
default) for a local test, and `tools/release/make_release_linux.sh` checks downloaded Linux files. A release made by hand
the old way still gets its Linux files from `linux.yml` when it is published.

## No game data, ever

Nothing of the game is in the repository, in CI or in a release. The workflow builds only from `engine/`, the official
tool downloads (SHA-256 checked) and earlier HeyListen parts, and refuses, twice, anything whose name looks like game data,
keys or a cache (`FORBIDDEN` in `make_release.py`, in every file and inside every zip): once when the Windows files are
packaged, once over all files before the draft. The Linux files go through `check_release_linux.py` as well.

## SignPath: what to set up once the project is approved

SignPath signs only what this repository builds: the setup and the builder's own `HeyListenBuilder.exe` and
`HeyListenBuilder.dll`. Never .NET's own files (Microsoft signs them), the prebuilt `third_party` files or the parts reused
from earlier releases; they are not even sent. The list is `SIGNED` in `make_release.py` and the artifact configuration;
change both together.

### In SignPath (app.signpath.io)

1. **Project**: repository URL `https://github.com/Sacrappy/HeyListen`. Note its **slug**.
2. **Trusted build system**: link the predefined *GitHub.com* trusted build system to the project (origin verification:
   SignPath then signs only artifacts uploaded by a workflow run of this repository on GitHub-hosted runners, and records
   the repository, commit, ref and run).
3. **Artifact configuration**: create one from
   [`.signpath/artifact-configurations/heylisten-builder.xml`](../.signpath/artifact-configurations/heylisten-builder.xml)
   (the root is a `<zip-file>`: GitHub artifacts are zips). Note its **slug**, e.g. `heylisten-builder`. When the file
   changes in the repository, update it in SignPath too.
4. **Signing policies** (SignPath Foundation sets up `test-signing` and `release-signing`):
   - `release-signing`: *Verify origin* on, approvers set (every release is approved by hand). **Allowed branch names**
     must accept the tag builds this workflow makes (`refs/tags/v*` runs): ask SignPath support how a tag run is matched
     before the first signed release, since their documentation shows branch patterns only (`main`, `release/*`).
   - `test-signing`: any branch, for dry runs.
5. **API token**: a CI user with the *submitter* role on both policies. Its token goes into GitHub as below.

### In GitHub (Settings, Secrets and variables, Actions)

| Name | Kind | Value |
|---|---|---|
| `SIGNPATH_API_TOKEN` | secret | the CI user's API token |
| `SIGNPATH_ORGANIZATION_ID` | variable | the organization's id (SignPath, Settings) |
| `SIGNPATH_PROJECT_SLUG` | variable | the project's slug |
| `SIGNPATH_SIGNING_POLICY_SLUG` | variable | `release-signing` (used for tags) |
| `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | variable | the artifact configuration's slug |
| `SIGNPATH_TEST_SIGNING_POLICY_SLUG` | variable, optional | `test-signing` unless set |
| `SIGNPATH_WAIT_SECONDS` | variable, optional | how long the job waits for the approval; `7200` unless set (keep under 3.5 hours: the job stops at 4) |
| `RELEASE_UPLOAD_PARTS` | variable, optional | `true`: the draft job uploads the new parts to `builder-parts` and `linux-parts` (public as soon as uploaded) |

Until the secret and the four first variables are all set, the signing job is skipped with a notice and everything else is
made unsigned. A secret is never printed: the workflow only tests whether it is set.

SignPath Foundation also asks for a *Code signing policy* section on the project's home and download pages (the free
signing by SignPath.io with a SignPath Foundation certificate, the team's roles: authors, reviewers, approvers, and a
privacy statement). That is the README's and the release notes' business, not the workflow's.

## A dry run

A dry run builds, signs with `test-signing` (if SignPath is set up) and checks everything, and leaves the files as the
run's artifacts (`release-files` holds the would-be release); no draft, no parts uploaded.

- **Before `release.yml` is on `main`** (GitHub offers *Run workflow* only for workflows on the default branch): push the
  branch under a `release-dry-run/` name. The version is `v0.0.0-dry.<run number>`.

  ```sh
  git push origin ci/release-workflow:release-dry-run/ci
  gh run list -R Sacrappy/HeyListen -w Release -b release-dry-run/ci
  git push origin --delete release-dry-run/ci      # afterwards
  ```

- **Once it is on `main`**: Actions, *Release*, *Run workflow*, from the branch to try, with a version that is not a release
  (`v0.4.1-dry.1`), `signing` `none` or `test-signing`, `draft` off:

  ```sh
  gh workflow run release.yml -R Sacrappy/HeyListen --ref <branch> -f tag=v0.4.1-dry.1 -f signing=test-signing -f draft=false
  ```

  Run from a tag with `draft` on, the same makes that tag's draft release, as a push of the tag does (to make it again
  after deleting a draft, for example).
