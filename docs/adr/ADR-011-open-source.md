# ADR-011: Rosc is free and open source under Apache-2.0

**Date:** 2026-11-26
**Status:** Accepted
**Deciders:** The owner (chose each point below on 2026-11-26)

## Context
Rosc was planned as a paid plugin. The owner has decided that it is free and open source. Nothing
has shipped, so the licence can be set before anyone takes a copy. The source holds no
third-party code (`THIRD_PARTY_NOTICES.md`). The AI models it offers are downloaded by the user
and are under MIT or Apache-2.0. Rosc is an Unreal Engine plugin, so its licence has to sit
beside the Unreal Engine EULA: a user's project combines Rosc with engine code that is not open.

## Decision
1. **Licence: Apache-2.0.** It is permissive, so a studio can use Rosc in a closed project. It
   grants patent rights in plain words. It grants no rights to the name (section 6), so a fork
   cannot ship as Rosc. Every source file starts with
   `// Copyright 2026 Fiavaion` and `// SPDX-License-Identifier: Apache-2.0`. The licence text is
   in `LICENSE` at the top of the repository and in the plugin's folder, so a packaged copy
   carries it.
2. **Where: a public GitHub repository, `github.com/Fiavaion/Rosc`.** The plugin's links point
   there: the repository for its maker and documentation, the issues page for support. There is
   no Fab listing for now; a free Fab listing, and the name check that goes with it, wait until
   the owner wants one.
3. **What is public: the product and its decisions.** The plugin, the host project, the build,
   test and package scripts, the design notes in `docs/` and these ADRs. The project's working
   notes (status, to-do list, lessons, research notes, the AI evals and the development
   instructions) stay on the owner's machine and are not tracked.
4. **The history is rewritten once, before the first push:**
   - the working notes are taken out of every commit;
   - the Android file-server token the engine wrote into the first commit's
     `DefaultEngine.ini` is taken out;
   - the author address becomes the owner's GitHub no-reply address.
   The history as it was is kept in a git bundle on the owner's machine, with a map from the old
   commit ids to the new ones. Commits that touched only the working notes are gone; each maps
   to the commit before it.

## Consequences
- Anyone may use, change and redistribute Rosc, including in paid work, under Apache-2.0.
  Camera and lens names in the database stay their owners' trademarks; the notices say so.
- Pricing, licence tiers and a paid listing are no longer planned. The shot assistant still uses
  the user's own Claude API key; Rosc adds no paid service.
- The rule that an AI model's weights must allow commercial use stands: Rosc's users make paid
  films with it.
- Pushing to the public repository publishes. It happens when the owner asks, as with any push.
- Commit ids from before 2026-11-26 changed. The ids named in the working notes were moved to
  the new ones; anything outside the repository that names an old id needs the map.

## Alternatives considered
1. MIT. It is shorter and common for Unreal plugins, but has no patent grant and says nothing
   about the name.
2. MPL-2.0. Changes to Rosc's own files would have to stay open, which is less usual for Unreal
   plugins and an extra step for studios.
3. GPL. Rejected: it cannot be combined with the Unreal Engine EULA in a user's project.
4. Publish the working notes too. Rejected by the owner: they hold local paths and the owner's
   open decisions.
5. Publish one fresh commit instead of the rewritten history. Rejected: the history explains
   the code.
