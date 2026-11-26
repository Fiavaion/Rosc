# ADR-012: A notice before a model is first downloaded

**Date:** 2026-11-26
**Status:** Accepted
**Deciders:** The owner (asked for the owners of the training data to be named, and for a notice
before download with as little friction as possible)

## Context
Rosc offers three models that the user downloads (ADR-009). The weights are under MIT or
Apache-2.0, both of which allow commercial use. A licence review on 2026-11-26 found two gaps:
- Some of the data the models were trained on is published by its owners for research or
  non-commercial use only (for example the Waymo Open Dataset, in MoGe-2's training set). The
  weights' licences say nothing about it, and whether such terms reach a model's output is not
  settled.
- The ONNX file Rosc fetches for MoGe-2 comes from a repository that states no licence; the
  author's original model is MIT by its tag.
Rosc's users make paid films, so they should know this before they rely on a model, and Rosc
should be able to show that they were told.

## Decision
1. Each model in `Resources/AI/models.json` says who holds its licence, any note on that licence,
   and the data it was trained on: each dataset's name, owner, terms, the page that states them,
   and whether they allow commercial use (yes, no or unclear). The list is checked when it is
   read, like the rest of the file.
2. The first time the user presses Download for a model, the model's row in the AI tab opens a
   notice: the licence and who holds it, the note, the datasets published for non-commercial use
   only and who owns them, how many more have terms that do not say, and a plain caution. It has **Agree and Download** and
   **Not Now**; every model's row also has **Training Data**, which opens the full table on a
   page beside the user guide (`Docs/TrainingData.html`). It is part of the tab, not a window: it
   can be reached by keyboard and read by a screen reader, and it asks nothing else.
3. Agreeing is kept in the user's own settings for the project, beside the agreements to send
   data (`URoscAISettings::AcceptedModelNotices`), as the model's id and a fingerprint of its
   notice. If the notice changes, it is shown again. A model already downloaded is not asked
   about, since the models folder is shared by all projects.
4. `URoscAISubsystem::DownloadModel` refuses a model whose notice has not been agreed, so no way
   into a download skips it.
5. The full table, with links to every owner's terms, is on that page and in
   `THIRD_PARTY_NOTICES.md`.

## Consequences
- One extra click, once per model. Nothing changes for a model already downloaded.
- The dataset facts are research, not legal advice, and are kept with their sources. A change to
  them changes the fingerprint, and users see the notice again.
- The notice states facts and a caution. It does not say that output may or may not be used
  commercially.

## Alternatives considered
1. A modal dialog. Rejected: more friction, it takes the focus away from the panel, and it is
   harder to drive by keyboard and in tests.
2. The facts on the page with no agreement. Rejected: weaker evidence that the user was told.
3. One agreement in the settings for all models. Rejected: it is made away from the moment of
   download, and it cannot follow a change to one model's notice.
