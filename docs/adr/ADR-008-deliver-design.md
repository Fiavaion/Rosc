# ADR-008: How Deliver builds and runs a render

**Date:** 2026-11-11
**Status:** Accepted (Spike #3 run 2026-11-11: every rule kept, no fallback taken)
**Deciders:** Claude, from a reading of the 5.8 Movie Render Pipeline source

## Context
ADR-002 puts delivery on Movie Render Graph. Deliver has to turn one choice (Review, VFX
handoff or Master) into a complete render of the sequence open in Sequencer, started from the
panel, with a slate, burn-ins, named files, camera and lens metadata and a camera for VFX. The
same path has to run unattended, so the phase's acceptance test can render with each intent and
read the files back.

What the 5.8 source says (file references are in `notes/claude-project-inventory.md`, "Movie
Render Graph in 5.8"):
- A `UMovieGraphConfig` can be built in C++ at run time (`CreateNodeByClass`,
  `AddLabeledEdge`, `AddInput`, `AddOutput`); none of it is editor-only, and an unsaved graph
  is accepted by a job.
- The "Render (Local)" button runs `UMoviePipelinePIEExecutor`. It can be given a private queue
  and reports the files it wrote through `OnIndividualJobWorkFinished`. It needs a real
  renderer: with `-nullrhi` the pipeline aborts.
- Each shot of a job can carry its own graph.
- There is a graph node for Apple ProRes (plugin AppleProResMedia, release quality, off by
  default, header private) and for multilayer EXR (Globals branch only). Depth, motion vectors
  and normals are post-process material passes of the deferred renderer. Object ID mattes are
  in the Beta plugin MoviePipelineMaskRenderPass.
- EXR headers get what the engine knows about the camera (focal length, f-stop, focus distance,
  sensor, transform) but nothing about the lens kit, ISO, ND or shutter angle. A Set Metadata
  Attributes node adds name and value pairs.
- Burn-ins are a property of each output node, drawn by a `UMovieGraphBurnInWidget` class whose
  only per-frame hook is a Blueprint event.
- There is no slate frame, and handle frames can only be added to both ends of a shot.
- ACEScg without an OCIO configuration asset comes from the project's Working Color Space
  (needs an editor restart) with the tone curve disabled; the EXR then carries those
  chromaticities.

## Decision
1. **A new editor module, `UEcineRender`**, holds everything that touches Movie Render
   Pipeline: building graphs, running a render, the burn-in widget. It takes plain data and
   knows nothing about the shot list or the panel; `UEcineEditor` gathers that data and calls
   it. Recipes that need no engine (what each intent asks for, file names, preflight rules,
   slate and burn-in text, the sidecar file) are pure functions in `UEcineCore`.
2. **Graphs are generated, one per shot, from engine nodes only.** A graph is built in memory
   for each shot when a render starts, with that shot's file names and metadata written into
   it, and is given to the shot (the first also to the job). Nothing in a graph is a UEcine
   node class, so a graph saved for a render farm loads without the plugin. The one soft
   reference is the burn-in widget class.
3. **Renders run in the editor through the PIE executor** on a private queue, with unsaved
   levels allowed. Deliver reports progress and the list of written files from the executor's
   delegates. (Spike #3.)
4. **Metadata has one source.** Camera body, format, lens, ISO, shutter angle, ND, T-stop,
   delivery frame, scene, shot and size go into the graph's Set Metadata Attributes node as
   `uecine/...` keys. EXR headers get them from the engine; the burn-in widget and the slate
   read the same attributes from the evaluated graph. What changes during a shot (focal
   length, aperture, focus distance, camera position) is already written per frame by the
   engine, and is also written to the sidecar file from the same samples bake for handoff uses.
5. **The burn-in widget is a native class**, `UUEcineBurnInWidget`, built in Slate. It takes
   the engine's per-frame Blueprint event by overriding `UObject::ProcessEvent`, which avoids
   shipping a Widget Blueprint that would have to be authored and kept in step by hand.
   (Spike #3.)
6. **The slate is the first frame of each review file**, drawn by the burn-in widget over a
   one-frame head handle. Because handles are symmetric, a slated review file also has one
   handle frame at its tail: frames = shot length + 2. With the slate off there are no handles.
7. **Review** is Apple ProRes 422 LT, display-referred, burn-ins on. **VFX handoff** is one
   multilayer EXR per frame, scene-linear in the project's working colour space, with beauty,
   depth, normals, motion vectors and Object ID mattes, plus the sidecar file and an FBX camera
   exported from the shot's handoff copy (a baked, unparented camera, so the FBX is in world
   space). **Master** is Apple ProRes 4444, display-referred, with temporal samples, no slate
   and no burn-ins. AppleProResMedia, MovieRenderPipeline and MoviePipelineMaskRenderPass
   become dependencies of the plugin; SequencerScripting (Beta) is used for the FBX export.
8. **ACEScg is checked, not forced.** When the project's working colour space is not ACEScg,
   preflight says so for VFX handoff and offers to set it (the editor then needs a restart).
   The metadata and sidecar name the colour space the files are really in.

## Spike #3 — decision rules (written before running)
Run in the unattended editor the tests use (`-RenderOffscreen`, Template_Default, unsaved),
from an automation test with latent commands, on a six-frame shot at 640 pixels wide.
- **Executor.** The PIE executor path is kept if a generated graph renders to completion,
  `OnIndividualJobWorkFinished` lists the files, the files are on disk with the expected frame
  count, and the open sequence is unchanged afterwards (playback range, shot sections). If the
  executor cannot start or finish unattended, the fallback is the same executor with a saved
  test map; if that also fails, Deliver stops and the decision returns to the user (a
  separate-process render would lose the file list and the unsaved state).
- **Burn-in.** The native widget is kept if text it draws is found in the rendered frame's
  pixels and its per-frame hook is called once per output frame. Otherwise the widget's
  per-frame data is read from the active pipeline at paint time; if that also fails, a Widget
  Blueprint is generated into the plugin's content by a tool script.
- **Per-shot graphs.** Kept if two shots render with different file names and different
  metadata values in one job. Otherwise one graph per job, and one job per shot.
- **ProRes by reflection.** Kept if the node class is found by path, its quality is set and a
  `.mov` is written. Otherwise Review falls back to the H.264 MP4 node (public header), and the
  brief's "ProRes" becomes a finding in the phase report.

## Spike #3 — result (2026-11-11)
`UEcine.Editor.Deliver.RenderTwoShots`, run with `tools/test.ps1 -Rendering`: two six-frame
shots of one unsaved sequence in Template_Default, rendered in one job in about a second.
- **Executor: kept.** The generated graphs rendered to completion unattended. The executor
  listed nine files, all on disk in the folder asked for: eight EXR files for the first shot
  and one `.mov` of eight frames for the second (six frames, a slate and a tail frame each).
  The sequence's playback range and both shot sections were unchanged afterwards.
- **Burn-in: kept.** The native widget's hook ran once per output frame (16 for 16). Its slate
  covered the first frame and its strip the bottom of the others, read from the EXR's burn-in
  layer; the `.mov` was decoded with ffmpeg by hand and showed the slate on frame 0 and the
  strip over the picture on frame 3.
- **Per-shot graphs: kept.** Each file was named for its shot and carried its own shot's
  metadata (`uecine/lens/kit` differed between the EXR header and the movie's slate).
- **ProRes by reflection: kept.** ffprobe reads the movie as ProRes 422 LT, 10-bit, Rec.709,
  24 fps, eight frames, with a timecode track.

What the run added to the design:
- In a multilayer EXR the slate and burn-ins are a layer of their own (`BurnIn`), not drawn
  into the picture: right for VFX, and the layer the tests read.
- Layers are named `FinalImage`, `Deferred_depth`, `Deferred_normal`, `Deferred_motion` and
  `beauty_ObjectID00…02`; depth is in centimetres from the camera.
- VFX handoff EXRs are packed with ZIP, one row at a time (lossless, and what compositing
  programs read fastest) instead of the engine's default PIZ. The tests read layers back with
  a reader of their own for this packing.
- Whether a render succeeded comes from the executor's finish, not the job's output data
  (LESSON-API-011). The engine logs one harmless error at the start of every graph render in
  the editor (LESSON-API-010).
- The output frame must have the shape of the camera's frame, or the picture is boxed inside
  it: the recipes size each render from the recording format and delivery frame.

## Decisions added while building the editor side (2026-11-11)
- **A delivery has one version.** UEcine fills in `{version}` itself: the latest version of any
  of the delivery's names already in the intent's folder, plus one, for every shot. The
  engine's own version counting is per file and would number the shots of one delivery apart.
- **Where files go.** `<delivery folder>/<sequence>/<Review|VFX|Master>/`. A VFX shot's frames
  are in a folder of the shot's file name, with its sidecar (`.json`) and camera (`.fbx`)
  beside them under the same name. The delivery folder defaults to `Saved/UEcine/Deliveries`.
- **A VFX handoff bakes for handoff first**, replacing the sequence's `_Handoff` copies, and
  each shot's FBX camera is exported from its copy at the time it has in the whole sequence.
- **One camera per shot.** A shot that cuts between cameras stops the render in preflight: its
  files could name only one camera package.
- **The colour space named in the files is the one the editor started in.** "Work in ACEScg"
  writes the project's setting and nothing else; it takes effect at the next start and is not
  undone with the editor's undo.
- **Preflight runs when asked**, not continuously: it plays to the start of each shot to find
  the camera the shot spawns.
- **Stop ends the play session the render runs in**, which is how the engine stops a render
  itself. A render is over only when that session is gone (LESSON-API-014). What a stopped
  render had written stays on disk.
- **The slate is drawn on the first frame the renderer keeps**, asked of the shot: the renderer
  also plays warm-up frames before it (LESSON-TEST-006).

## Consequences
- ✅ One code path for the panel button and the acceptance test.
- ✅ Graphs are standard data; nothing of UEcine is needed to load one.
- ✅ No hand-authored binary asset in the plugin.
- ❌ Renders need a renderer: the delivery tests only run with `tools/test.ps1 -Rendering`.
- ❌ A slated review file is two frames longer than its shot, not one.
- ❌ ACEScg depends on a project setting; without it VFX handoff files are scene-linear in
  Rec.709 primaries and say so.
- ❌ The editor is busy while a render runs (the engine's own local render behaves the same).
- ❌ The slate of a shot that starts on the first frame of the sequence is one frame before
  it, so the movie's timecode starts a frame early. Preflight says so and how to avoid it.
- ❌ A shot sequence used twice in a cut is baked as it plays the first time: the FBX camera of
  its second use is that of the first (its sidecar is its own).
- ❌ Exporting an FBX camera spawns the sequence's cameras in the editor level for a moment,
  as the engine's own export does.

## Alternatives considered
1. Saved graph assets in the plugin's content, adjusted by job variables — rejected: binary
   assets that cannot be reviewed or generated in this repository, and file names and metadata
   per shot are awkward to drive through variables.
2. A custom graph node that writes UEcine metadata each frame — rejected: a saved graph would
   then need the plugin, against the project's "standard data" rule.
3. Quick Render — rejected: no completion hook and no file list.
4. A slate rendered as a separate still beside the movie — rejected: the slate would not be in
   the review file.
5. A separate render process — rejected: needs saved assets, prompts to save, and returns no
   file list.
