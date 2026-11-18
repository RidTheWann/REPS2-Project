# Backup and recovery

The repository lost its remote history once (2026-11-12). This document records the
recovery layers that now exist, how to verify them, and what they do not cover.

## Layers

| Layer | Location | Independent of the working disk | Purpose |
| --- | --- | --- | --- |
| Working repository | `D:\REPS2-Project` | n/a | daily work |
| Local bundle | `%USERPROFILE%\REPS2-Backups\REPS2-Project-<stamp>.bundle` | **no** (same physical device) | fast restore of every ref |
| Private mirror | `https://github.com/RidTheWann/REPS2-Project-mirror` (private) | **yes** | off-device copy of all branches and tags |
| Scheduled task | `REPS2-Backup`, daily 02:00 | n/a | produces both layers unattended |

The local bundle and the working repository live on the same NVMe device
(Disk 0, drive letters `C:` and `D:`). The bundle is therefore **not** an
off-device backup; the private mirror is the only copy that survives loss of the
machine. Do not record the bundle as the primary backup.

## Bundle contents

`git bundle create --all` captures `refs/heads/*`, `refs/remotes/*` and
`refs/tags/*`. Worktree-specific branches are included because they are ordinary
branches; nothing under `.worktrees/` is committed.

## Running a backup

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/backup/Backup-REPS2.ps1
```

The script:

1. writes `REPS2-Project-<stamp>.bundle` into `-Destination`
   (default `%USERPROFILE%\REPS2-Backups`),
2. runs `git bundle verify`,
3. restores the bundle into a temporary bare repository with
   `git fetch <bundle> "+refs/*:refs/*"`, runs `git fsck --full`, and requires the
   restored `refs/heads` and `refs/tags` to match the source exactly,
4. pushes `+refs/heads/*:refs/heads/*` and `+refs/tags/*:refs/tags/*` to the
   `backup-mirror` remote (no deletions, no `--mirror`),
5. deletes bundles beyond `-KeepBundles` (default 14),
6. appends one JSON record to `<Destination>\backup-log.jsonl`.

Exit code `0` means every enabled stage succeeded. Any failure is appended to the
log with `"outcome": "failed"` and returns exit code `1`.

Useful switches: `-SkipMirror`, `-SkipRestoreTest`, `-Destination <path>`,
`-KeepBundles <n>`, `-MirrorRemote <name>`, `-RegisterTask`.

## Mirror

`backup-mirror` is a separate private repository, not a fork and not a remote of
`origin`. It must stay private. Synchronise refs only; never run
`git push --mirror`, which would delete remote refs that no longer exist locally.

To rebuild the mirror on another machine:

```powershell
git remote add backup-mirror https://github.com/RidTheWann/REPS2-Project-mirror.git
git push backup-mirror "+refs/heads/*:refs/heads/*" "+refs/tags/*:refs/tags/*"
```

## Restore procedure

```powershell
git clone <bundle-or-mirror-url> REPS2-Project
git -C REPS2-Project fsck --full
```

For a full ref-for-ref restore (branches and tags, not just the default branch):

```powershell
git init --bare restored.git
git -C restored.git fetch <bundle> "+refs/*:refs/*"
git -C restored.git fsck --full
```

## Verified backup record

| Field | Value |
| --- | --- |
| Source repository | `https://github.com/RidTheWann/REPS2-Project` (public) |
| Source HEAD at verification | `1cf3afdd8919a256b504c2efc5d290bafc644e20` |
| Frozen bundle | `C:\Users\RidTheWann\REPS2-Backups\REPS2-Project-20261118-115712.bundle` |
| Bundle SHA-256 | `479CB898C2A74C8E2FCCACEE0F64ACA3E4A1A7A970B0D58711515459436C1792` |
| Bundle size | 4,333,630 bytes |
| Refs captured | 162 |
| `git bundle verify` | ok |
| Restore test | temporary bare fetch of `+refs/*:refs/*`, `git fsck --full` clean, source and restored `refs/heads`/`refs/tags` identical |
| Mirror | `RidTheWann/REPS2-Project-mirror`, private, empty at creation, 34 heads+tags pushed and compared ref-for-ref |
| Scheduled task | `REPS2-Backup`, daily 02:00, next run 2026-11-19 02:00 |

The older `REPS2-Project-20261104-045206.bundle` (1,448,206 bytes) is retained as
the 2026-11-04 recovery artifact; it is superseded but not deleted.

## Branch protection (observed 2026-11-18)

`main` protection, read through `gh api repos/RidTheWann/REPS2-Project/branches/main/protection`:

- force pushes disabled, branch deletion disabled;
- `enforce_admins` enabled (no administrative bypass);
- required status checks, `strict` (branch must be up to date):
  `Rust frontend (MSRV 1.88)`, `Rust frontend (ubuntu-latest)`,
  `Rust frontend (windows-latest)`;
- conversation resolution required;
- 0 required approving reviews.

The required-check list covers the Rust frontend only. CMake/engine and desktop
workflows run on pull requests but are not required checks. Widening the required
set would change the merge policy for every future PR and is left as an explicit
owner decision rather than being altered silently.

## Limitations

- No off-device copy exists other than the GitHub mirror; no external drive or
  network share is attached to this machine.
- The mirror inherits GitHub's availability and the account's credentials.
- The scheduled task runs only while the machine is on and the user is logged in
  (`Logon Mode: Interactive only`); missed runs are caught up by
  `-StartWhenAvailable` at the next opportunity.
- Bundles on the working device do not protect against device failure.