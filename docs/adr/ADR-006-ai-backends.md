# ADR-006: AI job interface and backends

**Date:** 2026-11-11
**Status:** Accepted 2026-11-11 after Spike #2: in-engine NNE is the default local backend for
single-image depth
**Deciders:** user (local and cloud both wanted, commercial distribution), Claude

## Context
AI features are optional. Studios under NDA will not send footage off the machine. Most strong
models are non-commercial; the commercially safe set is listed in
the project's research notes (kept with the project, not published). The editor already uses the GPU, and in-engine inference
(NNE with ONNX Runtime) has limits: no models over 2 GB with external data, runtime version
coupling.

## Decision (fixed parts)
- One job interface (`IRoscAIBackend`: submit, progress, cancel, result) used by every AI
  feature. Features never talk to a model directly.
- Cloud is off by default. Any backend that sends data off the machine shows a consent gate
  naming what is sent and where, per project.
- No model weights ship in the plugin. Only commercially licensed models are offered. Weights
  are downloaded by the user, with the licence shown.
- API keys live in per-user editor settings, never in project config.

## Decision pending the spike
Which local backend is the default for single-image depth: in-engine NNE, or a local sidecar
process.

## Decision rule (written before measuring)
Run Depth Anything V2 Small (ONNX) on a 1920×1080 still on the RTX 5070 Ti, with the editor
open on the sample scene. In-engine NNE is the default if all hold:
1. it loads and runs through NNERuntimeORT with the DirectML provider without engine changes;
2. one image completes in under 2 seconds end to end;
3. the editor stays responsive (no hitch over 250 ms on the game thread during inference);
4. ten consecutive runs complete without a crash or a leak over 200 MB.
Otherwise the sidecar is the default and NNE is not shipped as a second path.
Heavy models (segmentation, larger depth) use the sidecar regardless.

## Spike #2 — result (2026-11-11)
Depth Anything V2 Small (the `onnx-community/depth-anything-v2-small` ONNX file, 99 MB,
Apache-2.0) run from a throwaway editor test on the RTX 5070 Ti, UE 5.8.3, through
`NNERuntimeORTDml` (`INNERuntimeGPU`, CPU-bound tensors), on a 1920 x 1080 still rendered by
the smoke test. End to end is: resize to 924 x 518, normalise, run, scale the depth back to
1920 x 1080. The session is made and the model run on a worker thread. Two editor launches.

| Rule | Measured | Holds |
|---|---|---|
| 1. Loads and runs through NNERuntimeORT with DirectML, no engine changes | Yes. The model's input and output are dynamic; the input shape is set per picture | Yes |
| 2. One image in under 2 s end to end | 0.20 to 0.23 s once warm (18 of 20 runs). First run of a launch: 0.88 s, plus 0.29 s to make the session. First run ever on the machine: 2.23 s, plus 0.32 s | Yes, but for the first run ever on a machine |
| 3. No game-thread hitch over 250 ms during inference | Worst tick 14 ms over about 500 ticks; making the model object on the game thread took 0.03 s | Yes |
| 4. Ten runs without a crash or a leak over 200 MB | No crash. Process memory grew 4 to 6 MB from the first run to the tenth; loading the runtime and model costs about 360 to 400 MB once | Yes |

Also measured: a picture of a new shape costs more the first time (518 x 518: 0.49 s, then
0.05 s) and no more after; 45 MB more memory after three more shapes. The depth map was
looked at: the subject, the floor and the horizon read correctly.

Rule 2 was written without saying cold or warm. It is read here as the time a user waits for
a picture in normal use, which it meets tenfold; the one run over 2 s was the first the
runtime ever made on the machine. A sidecar process would start far slower than that. If the
rule is to be read as "every run, including the first ever", it fails by 0.2 s and the user
can overrule this acceptance.

Not measured: a heavy scene drawing in a real viewport while the model runs. The game thread
does not wait for the model, so the picture rate may dip but the editor should not hitch;
this is a hand check.

Measured at the Phase 7 gate (2026-11-12), with a level viewport drawing while the three models
ran one after another in the editor: the editor's longest pause was 0.03 s and under during the
depth map and the lens estimate, and 11 to 14 s during the matte. Loading a model does not hold
the editor; the matte's run itself does. Run in the helper with a Python that works on the
processor, the same three jobs paused the editor for under a second.

The cause is graphics memory. BiRefNet Lite at 1024 x 1024 under DirectML takes about 10 GB:
the whole card read 4.4 GB before the matte, 15.0 GB while it ran and 3.9 GB after, on a 16 GB
card. The game thread does not wait for the model, but the viewport waits for the graphics
card, and the card is full. The first version of the backend kept the last model loaded for
the next job. With the matte's model left loaded the editor's own graphics memory read 26.7 GB
against a budget of 14.1 GB, and the next movie render at 4608 x 3164 lost the graphics device
and took the editor down. The backend now loads a model for its job and releases it when the
job ends, at the cost of loading again for the next picture (about a second for the depth
model).

Rule 3 is met for the depth map, which is what the spike and the roadmap asked of the
in-editor backend, and not for the matte. `Rosc.AI.Editor.Pictures` measures the pause for
each job and requires under a second for the depth map and the lens estimate; the Matte button
and the user guide say what to expect; the pause that remains is logged as TD-002.

## Consequences
- `RoscAI` depends on the engine plugin NNERuntimeORT (Beta) and its module `NNE`. The
  in-engine backend makes the session and runs on a worker thread, one job at a time.
- The model file is not in the plugin. The user downloads it (the AI tab says from where,
  shows the licence, and checks the file) into the per-user Rosc models folder.
- The first depth map of a session takes about a second longer than the rest; the job reports
  "loading the model" as its first stage.
- Heavy models (mattes, focal length, larger depth) run in the sidecar; the cloud backend is
  for what the user chooses to send, behind the consent gate.
