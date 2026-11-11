# ADR-003: The editor UI is native Slate with its own style set

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, from research

## Context
The product is judged on UI quality. Unreal offers native Slate (C++), Editor Utility Widgets
(UMG assets) and an immediate-mode Slate plugin. The choice decides how every panel is written.

## Decision
All UI is native Slate in `UEcineEditor`, with one style set (`FUEcineStyle`) holding colours,
fonts, icons and spacing. The panel is a dockable nomad tab with one sub-tab per department.
Custom controls (lens selector, exposure strip, move timeline) are Slate widgets.

## Consequences
- ✅ Full control of layout, input, focus, undo and performance; matches the editor's look.
- ✅ No binary UI assets to version; diffs are reviewable; Fab compiles it like any code.
- ❌ Slower to iterate than visual UMG editing.

## Alternatives considered
1. Editor Utility Widgets — rejected: weaker keyboard and focus control, binary assets,
   harder to keep consistent across engine versions.
2. Immediate-mode Slate — rejected: aimed at debug tools, not a polished product UI.
