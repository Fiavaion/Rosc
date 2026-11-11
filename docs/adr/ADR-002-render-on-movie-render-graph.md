# ADR-002: Render and multi-pass output are built on Movie Render Graph

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, from research

## Context
Unreal has two offline render paths: the older Movie Render Queue settings and the Movie
Render Graph. In 5.8 the graph is in the release-quality MovieRenderPipeline plugin, is
described as production-ready, and is where render layers, collections, modifiers and
metadata nodes live. Building delivery on one of them shapes every render feature.

## Decision
Deliver generates and drives Movie Render Graph configurations (graph assets, custom nodes
where needed, job variables). No delivery feature is built on the legacy queue settings.
Known graph gaps (high-resolution tiling, some Object ID cases with the Path Tracer) are
surfaced by the preflight check as plain warnings, not worked around with a second render path.

## Consequences
- ✅ One render path; aligned with where Epic is investing.
- ✅ Render layers and per-layer modifiers are available for mattes and holdouts.
- ❌ Features missing from the graph are unavailable until Epic adds them.

## Alternatives considered
1. Legacy queue settings — rejected: no render layers, and it is the path being left behind.
2. Supporting both — rejected: duplicate code paths, which this project does not allow.
