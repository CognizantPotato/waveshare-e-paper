# Repo Maintenance

This repository is a private, long-lived base for personal e-paper projects.
It only pulls changes from the official Waveshare repository and does not push
changes back upstream.

## Branch model

- `master`
  Retired historical branch. It is no longer intended for ongoing work and may
  only remain locally for reference.
- `archive/master-2026-03-29`
  Explicit archive copy of the old `master` state before the repository was
  re-centered around the upstream-syncable starter branch.
- `codex/http-image-retriever-snapshot`
  Snapshot of the previous custom layout where `Arduino_R4` was removed.
- `vendor/upstream`
  Vendor tracking branch that mirrors the official Waveshare branch used for
  updates.
- `starter/http-image-retriever`
  Reusable base branch for future projects and future downstream repositories.

## Remote model

- `origin`
  Your personal GitHub fork.
- `upstream`
  Official Waveshare repository: `https://github.com/waveshareteam/e-Paper.git`

As of 2026-03-29, the official default branch is `upstream/master`, not `main`.

## Normal project flow

Use `starter/http-image-retriever` as the base for new work:

```bash
git checkout starter/http-image-retriever
git checkout -b my-project-branch
```

If you want a separate repository for a project, fork or clone from
`starter/http-image-retriever` rather than from `vendor/upstream`.

For GitHub repository settings, `starter/http-image-retriever` should be the
default branch so scheduled sync automation runs there and all new work starts
from the maintained base.

## Upstream sync flow

The GitHub Actions workflow in `.github/workflows/upstream-sync.yml`:

1. Fetches `upstream/master`
2. Force-updates `origin/vendor/upstream` to match that vendor state
3. Opens or reuses a pull request from `vendor/upstream` into
   `starter/http-image-retriever`

This keeps vendor ingestion automatic while preserving a review step before
changes land in your reusable starter branch.

If the PR merges cleanly, your starter branch picks up the upstream vendor
changes alongside your custom project files. If the PR shows conflicts, resolve
them in the PR or locally and merge intentionally.

## Manual sync commands

If you ever want to run the update flow locally:

```bash
git fetch upstream
git checkout vendor/upstream
git reset --hard upstream/master
git push --force-with-lease origin vendor/upstream
git checkout starter/http-image-retriever
git merge vendor/upstream
```

Only run the merge locally when you want to resolve conflicts by hand instead of
through the pull request flow.
