# ADR-007: The shot list is read from Sequencer; shot notes live in the shot's sequence

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, from the engine's 5.8 headers

## Context
The Shots department keeps a shot list (scene, shot, size, lens, support, move, notes),
storyboard frames and an overhead diagram, and exports a shot list and a camera report. What a
user types is saved into their project, so where it is stored is hard to change later. Three
homes were considered:

- **A. Metadata objects in the shot's Level Sequence.** `ULevelSequence::FindOrAddMetaData`
  keeps editor-only objects inside the sequence asset. The engine uses it for its own
  `UMovieSceneMetaData` (author, created, notes) and for shot ratings. Lookups skip null
  entries, so a sequence opens with the plugin removed; the UEcine object is simply dropped.
- **B. Key and value tags in the package's metadata.** No class to go missing, but no undo,
  no types, and nowhere sensible for a storyboard picture.
- **C. A separate shot list asset** that names sequences by path. Survives anything, but it
  is a second source of truth that drifts when a shot is renamed, duplicated or re-ordered.

## Decision
A, and nothing is stored that Sequencer already knows.

- **The list itself is not stored.** It is read from the sequence open in Sequencer: one row
  per section of its Shot track in time order, or the sequence itself as a single shot when
  it has no Shot track. Shot name, order, start and duration are Sequencer's.
- **Lens, camera, support and focus are not stored.** They are read from the shot's camera
  (its Cine Camera, UEcine camera package and rig) when the list is shown or exported.
- **Notes** use the engine's own `UMovieSceneMetaData::Notes` on the shot's sequence, so they
  also show in Sequencer's own metadata editor.
- **Scene, shot size, move and storyboard frames** are a `UUEcineShotMetaData` object
  (`UEcineRuntime`) in the shot's sequence. Storyboard frames are kept as JPEG data, at most
  640 pixels wide, so a shot's pictures travel with it.
- **Paperwork is plain files:** CSV for the shot list and the camera report, and one
  self-contained HTML page (pictures and diagrams inside it) that prints to PDF from any
  browser. The writers are pure functions in `UEcineCore`.

## Consequences
- ✅ Re-ordering, renaming, trimming or duplicating shots in Sequencer needs no bookkeeping:
  the list follows, and a duplicated shot keeps its notes.
- ✅ Every edit is an ordinary transaction on an object in the sequence, so it can be undone.
- ✅ A project opens without the plugin. The sequences play as before; only the UEcine shot
  fields are gone, and the notes remain.
- ❌ The data is editor-only (`WITH_EDITORONLY_DATA`), like all sequence metadata. A cooked
  game cannot read it, which is acceptable for paperwork.
- ❌ Each storyboard frame adds roughly 100 to 300 KB to its sequence asset.
- ❌ `UUEcineShotMetaData` is a shipped class once released: renaming it or its properties
  needs a redirect and explicit approval (CLAUDE.md, Change Boundaries).
- Shot numbering follows Sequencer's own project settings (prefix, first number, increment,
  digits) through `MovieSceneToolHelpers`, not a UEcine scheme.
