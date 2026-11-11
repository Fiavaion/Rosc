# ADR-006: AI job interface and backends

**Date:** 2026-11-11
**Status:** Proposed — pending Spike #2 (run at the start of Phase 6)
**Deciders:** user (local and cloud both wanted, commercial distribution), Claude

## Context
AI features are optional. Studios under NDA will not send footage off the machine. Most strong
models are non-commercial; the commercially safe set is listed in
`notes/claude-project-inventory.md`. The editor already uses the GPU, and in-engine inference
(NNE with ONNX Runtime) has limits: no models over 2 GB with external data, runtime version
coupling.

## Decision (fixed parts)
- One job interface (`IUEcineAIBackend`: submit, progress, cancel, result) used by every AI
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

## Consequences
Recorded when the spike completes and this ADR is accepted.
