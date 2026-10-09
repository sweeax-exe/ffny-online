# HD v0.6.0 installer source review

Date: 2026-10-08. Bounded read-only review of `scripts/build-hd-pack.py`, `scripts/hd_corpus.py`, `setup/engine.py`, `scripts/package-setup.py`, `scripts/check-setup-assets.py`, and installer unit-test sources. No builds, tests, game runs, media generation, installation, or publication performed. This report is the only file written by the reviewer. Findings describe the inspected working tree, which the primary agent is updating concurrently.

Final source re-read: all defects found by this review are resolved. The descriptions below preserve their original triggers and record the fixes. The primary agent reports 246 project tests passing under the Visual Studio environment; that execution was not repeated by this reviewer. Tracked-source packaging and the separate runtime/release acceptance gates still apply.

## Resolved during review: Embedded Python sibling-module imports

Original locations: `scripts/package-setup.py:140–141`, `scripts/build-hd-pack.py:16–17`, `scripts/check-setup-assets.py:49–50`.

The packaged `python313._pth` lists the standard library, executable folder, site-packages, `../source`, and `../source/tools/xboxrecomp`. It omits `../source/scripts`. The HD entry point immediately imports sibling modules `hd_assets` and `hd_corpus` without explicitly adding its own directory. A normal developer Python can resolve those siblings; the packaged isolated interpreter overrides normal search-path initialization. The optional installer step therefore has a concrete missing import path. The existing embedded-runtime probe imports Pillow, NumPy and toolkit modules but does not exercise the HD entry point.

Add the scripts path to the packaged interpreter configuration, or bootstrap the entry point's reviewed directory explicitly. Verify the actual embedded interpreter can run the packaged HD script's `--help` and import its siblings. Python's [official _pth documentation](https://docs.python.org/3.13/library/sys_path_init.html#pth-files) confirms that the file completely overrides `sys.path` and enables isolated mode. This finding was established from source/configuration; no interpreter invocation was performed by this reviewer.

**Re-read resolution:** the primary agent added `../source/scripts` to the packaged path configuration (`package-setup.py:146–147`) and the asset checker now probes `build-hd-pack.py --help`. The missing-path source defect is resolved; the primary agent owns execution of the new check.

## Resolved omission detection; tracked HD sources remain a packaging gate

Locations: `scripts/package-setup.py:53–59,100–103`, `scripts/check-setup-assets.py:27–50`.

Read-only Git inspection found these five required files untracked: `scripts/build-hd-pack.py`, `scripts/hd_assets.py`, `scripts/hd_corpus.py`, `scripts/hd_corpus.html`, and `config/hd-runtime-aliases.json`. Packaging copies `git ls-files` only. Its dirty-tree check explicitly ignores untracked files, and payload inspection checks the integrity/allowlist of files that exist without requiring these inputs. A payload created from this working state can therefore pass inspection while its HD checkbox fails because required files were never included. The HTML file is also required by `hd_corpus.build`, which copies it after completing the image work.

This is a release-readiness gate, not authorization to commit or publish. Track the reviewed source files before release packaging and require the complete HD source/input set in packaging or payload validation. The validation should catch this omission even for local development payloads.

**Re-read resolution:** packaging now requires `HD_SOURCES` after copying tracked inputs (`package-setup.py:116–118`), and payload inspection independently requires those files (`check-setup-assets.py:32–34`). A missing-HD-source payload can no longer silently pass those source checks. The reviewed source files must still be tracked before a payload can be prepared.

## Resolved during review: Interrupted cache-record resume

Locations: `scripts/hd_corpus.py:101–107,149`, `setup/engine.py:235–241`.

Per-image JSON records are saved directly with `write_text`, while resume immediately calls `json.loads` on any existing record. The installer cancels by forcibly terminating the builder process tree. If termination occurs after a record has been truncated but before its new contents are complete, the next run enters the same fingerprint directory and fails JSON parsing before reaching the hash checks that would regenerate the cache entry. The installer advertises resuming completed work, but this interrupted output prevents it.

Publish records with a temporary file and atomic replacement. Treat an incomplete or unreadable cache record as a rebuild request; generated images already have the atomic replacement needed to protect installed hard-links. Add a synthetic interrupted-record resume fixture. The current tests cover modified linked PNGs but not malformed cache JSON.

**Re-read resolution:** the primary agent added guarded cache-record parsing and canonical path checks (`hd_corpus.py:101–111`) plus atomic record publication (`157–160`). The source defect is resolved; execution of a malformed-record fixture belongs to the primary agent's validation.

## Resolved during review: HD activation and rollback boundary

Original locations: `setup/engine.py:502,514–526,557–558`; the original tests covered failure during the builder command only.

During an update, successful generation immediately removes the previous generated pack names from the effective settings and enables the new ones. The installer then enters `activate`, replaces the launcher and invokes the shortcut subprocess before publishing its install receipts. If that subprocess fails or cancellation is observed there, setup reports failure while the previous install receipts still select the old installation and its HD settings have already changed. There is no settings rollback. A cancellation marker arriving just after the builder exits is also not checked before `enable_hd_packs` writes the settings.

Defer HD settings publication until the final activation boundary, with an explicit cancellation check before committing, or restore their exact prior bytes on activation failure. Keep the generated files available for resume. Test cancellation/failure after builder success with an existing installation and assert its effective pack selection remains unchanged.

**Re-read resolution:** `build_hd_textures` now prepares the generated pack list without writing active settings (`engine.py:490–501`). `activate` runs shortcuts and explicitly checks cancellation before taking byte snapshots and publishing HD selection/receipts (`516–534`). Exceptions during final publication restore exact prior bytes, or remove files that did not previously exist (`535–543`). The source now includes parametrized shortcut-failure, cancellation, receipt-failure, and successful-activation fixtures in `test_hd_setup.py`, plus an assertion that successful generation alone does not change settings. The reported HD settings preservation defect is resolved.

## Resolved earlier findings and review limits

The earlier hard-link overwrite issue is fixed: regenerated upscaled images use a temporary PNG and replace the corpus path (`hd_corpus.py:132–136`), preserving a modified installed inode for `assemble` to reject. A synthetic test now covers that path. Padded and duplicate texture sections are now handled using the effective final pack value, and a test covers both. Hash/dimension checks, builder ownership markers, separate pack-wide mip policies, and preservation of unrelated canonical settings are present.

There are no remaining open defects established by this bounded source review. Runtime corpus coverage, mixed material semantics, clean-machine installation, actual cancel/repair execution, and user-visible HD acceptance remain separate validation work; this source review does not certify them.
