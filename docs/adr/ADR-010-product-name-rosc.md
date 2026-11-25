# ADR-010: The product is called Rosc

**Date:** 2026-11-25
**Status:** Accepted
**Deciders:** The owner (chose the name from a shortlist on 2026-11-25)

## Context
The plugin was built as "UEcine". The owner wanted a name that fits a camera department and is
an Irish word. Nothing has shipped: no user has saved a level, a sequence or a settings file
that names a class or a module of the plugin, so a rename costs no redirects now and would cost
them after a release.

## Decision
1. The product is **Rosc** (said "rusk"): a literary Irish word for "eye".
2. The name replaces the old one everywhere, with no alias and no redirect:
   - the plugin (`HostProject/Plugins/Rosc`, `Rosc.uplugin`) and its six modules (`RoscCore`,
     `RoscRuntime`, `RoscMonitor`, `RoscRender`, `RoscAI`, `RoscEditor`);
   - every class, struct, enum and namespace (`FRoscOptics`, `URoscCameraPackageComponent`,
     `ARoscRig`, `SRoscPanel`, `Rosc::`), and the API macros (`ROSCCORE_API`);
   - content (`/Rosc/Sample/RoscSampleStage`), the panel's tab (`RoscPanel`), the settings
     pages ("Rosc", "Rosc AI"), console variables and test names (`Rosc.*`);
   - what is written into files: metadata keys (`rosc/...`), the sidecar schema
     (`rosc.sidecar/1`), the picture server's protocol (`rosc-ai/1`) and script
     (`rosc_ai_server.py`);
   - folders: `Saved/Rosc/...` in a project, `%LOCALAPPDATA%\Rosc\Models` for models;
   - the host project (`RoscHost.uproject`), the tools, the guide and the notes.
3. The sample level was rebuilt by its test, not renamed as a file: a level stores the names of
   its package and of the classes in it.

## Consequences
- Anything outside this repository that drove the plugin by its old names stops working until
  it is changed: the how-to films' project (host project path, map path, tab id, window title,
  and every line of narration that says the name).
- A file delivered before the rename has `uecine/...` metadata keys; one delivered after has
  `rosc/...`. No tool of ours reads delivered files by key except the tests.
- Commit messages before 2026-11-25 say UEcine. History is not rewritten.
- A name search was a quick web search only. A trademark search and a check of the Fab listing
  name are still owed before release (`notes/TODOs.md`).

## Alternatives considered
1. Keep the code names and change only what the user sees — rejected: two names for one thing
   in every file, and the old one would ship in class names, paths and metadata for good.
2. Rename with class and package redirects — rejected: redirects are for users' saved data,
   and there is none yet.
