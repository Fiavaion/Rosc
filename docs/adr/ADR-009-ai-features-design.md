# ADR-009: AI features — jobs, backends, models, consent and the shot assistant

**Date:** 2026-11-11
**Status:** Accepted
**Deciders:** Claude, within ADR-006 and the change boundaries in `CLAUDE.md`. Two choices here
are the user's to overrule and are flagged in the Phase 6 report: no third-party cloud vendor
for pictures, and Claude as the shot assistant's language model.

## Context
ADR-006 fixed one job interface, three kinds of backend, cloud off by default behind a consent
gate, no weights in the plugin, keys in per-user settings. Spike #2 made in-engine NNE the
default for single-image depth. This record fixes what is hard to change later: the shape of a
job, how the plugin talks to a helper process or a remote machine, where models live, what
consent means, and how the shot assistant acts on the scene.

## Decision
1. **Module `RoscAI` (Editor).** It owns jobs, backends, the model list and consent. The AI
   tab is in `RoscEditor`. Nothing else in the plugin depends on it: with no model installed
   and no server set, every other department works as before.
2. **A job is a task on one picture.** Tasks: depth, matte, focal length. A request names the
   task and the picture, a file on disk (a frame grab is written to a file first). A result is
   a picture (depth or matte) or a field of view. Jobs run one at a time, off the game thread,
   report a stage in words and a fraction, and can be cancelled. `URoscAISubsystem` runs
   them; backends implement `IRoscAIBackend` (the host it sends to, whether it can run a
   task and if not why, run, progress, cancel).
3. **Three backends, two implementations.**
   - *In the editor*: NNE with ONNX Runtime on DirectML. All three tasks run here.
   - *Helper on this machine*: `Resources/AI/rosc_ai_server.py`, a small server the plugin
     starts with the user's own Python (it needs `onnxruntime`, `numpy` and `pillow`),
     listening on 127.0.0.1 only, with a key made for that run. It stops when the editor does:
     it watches its input, which closes with the editor. It is for machines where the
     in-editor runtime is not available, and is where a model with no ONNX file would go.
   - *Server elsewhere*: the same script on a machine the user or studio runs, reached by URL
     with a key. This is the cloud backend: it is the user's own compute, and no third-party
     paid service is built in. The script refuses to listen beyond its own machine without a
     key.
   The helper and the remote server speak one protocol, `rosc-ai/1`, over HTTP:
   `GET /v1/health` (protocol, tasks it can do), `POST /v1/jobs?task=` (the picture as PNG, at
   most 2048 pixels on its long side), `GET /v1/jobs/{id}` (state, stage, fraction, result),
   `GET /v1/jobs/{id}/picture` (the result as PNG), `DELETE /v1/jobs/{id}`. One client class
   serves both; only the address, the key and the consent gate differ.
   Both implementations read one recipe per model from the model list (input size, mean and
   deviation, outputs), so a picture gives the same result wherever it is worked on.
4. **Consent.** A backend that sends anything off the machine cannot be given a job until the
   user has agreed, for this project and that host, in the AI tab beside text that names what is sent (the
   picture; or for the assistant the words typed and a description of the cameras and shots,
   never pictures) and where. Agreement is kept in per-user project settings and can be
   withdrawn in the AI tab. The check is in the subsystem, not the tab, so no caller can skip
   it. 127.0.0.1 is this machine and needs no consent.
5. **Models.** `Resources/AI/models.json` lists each model offered: task, name, licence and
   its page, where the file comes from, size, SHA-256 and the recipe to run it. Listed:
   Depth Anything V2 Small (depth, Apache-2.0), BiRefNet Lite (matte, MIT) and MoGe-2 Small
   (focal length, MIT). Only models whose weights are published for commercial use are
   listed; RMBG is not, being non-commercial. What was checked is each model's card and
   licence tag, not the licence text or the training data, and MoGe's MIT is a tag only:
   a fuller licence review is owed before a release. Files live in a per-user folder
   shared by all projects (`%LOCALAPPDATA%/Rosc/Models`, or the folder in the settings). The
   AI tab shows the licence and downloads a file only when the user presses Download; a
   download is hashed before it is given its name, and a file is hashed again each time a
   model is brought into memory, so a file changed or put there by hand is caught.
6. **Results are files.** Depth is a 16-bit grey PNG (near is bright; it is relative depth,
   not a distance, and the tab says so); a matte is an 8-bit grey PNG. They are written to
   `Saved/Rosc/AI/<picture name>/` and shown in the tab. A focal-length estimate is shown as
   a field of view and as the focal length on the panel camera's recording format, with one
   button that puts the nearest lens of the camera's kit on, as one undoable change.
7. **The shot assistant plans, the user applies.** The user describes a shot in words. The
   plugin sends the words and a short description of the scene (cameras, the panel camera's
   package and support, subjects by name and position, the open sequence's shots) to a
   language model and gets back a plan: an ordered list of typed actions from a small fixed
   set (choose body, format, lens; set exposure; put on a support; set marks and timing;
   focus on or follow a subject; add a shot; bake the move). The plan is shown in film
   words. Nothing changes until the user presses Apply; then every action runs through the
   same editing functions the tabs call, inside one transaction, so one undo takes the whole
   plan back. Actions that the gear cannot do are flagged, as in the Grip tab.
   One request, one structured answer: no agent loop and no tool results sent back.
8. **The assistant's model is Claude, through the Messages API, with the user's own key.**
   It is a cloud backend: off until a key is set and consent given. The key is in per-user
   editor settings. The provider sits behind one interface (`IRoscAssistantModel`) so that
   another can be added without touching the plan or the tools. The request is plain HTTPS
   from the engine's HTTP module (there is no C++ SDK).
9. **Evals for the assistant** are cases of a description and a scene with checks on the plan
   (right support, a lens in the kit, the subject named, a move within limits), run by a
   script against the live model, with the model and its score recorded as the baseline.
   Running them costs the key's owner money, so they run when the user says so.

## Consequences
- The acceptance run (a depth map for a test still) needs the depth model in the models
  folder; the test skips, and says so, when it is not there.
- The helper needs Python and packages the user installs; the tab says what is missing in
  words. The helper's client is tested against a small stand-in server.
- A studio that wants a hosted service can put any service behind the protocol.
- The assistant cannot do anything the tabs cannot: a new ability is a new typed action over
  an existing editing function.

## Alternatives considered
- *A third-party inference service built in.* Rejected for now: it is a paid service and a
  vendor choice, which is the user's to make.
- *Let the model call tools in a loop on the live scene.* Rejected: slower, harder to preview
  and to undo as one step, and the research found engine tool loops error-prone.
- *Run every model in the editor.* Rejected by ADR-006: large models do not load through NNE.
