# UEcine — third-party notices

This file lists everything in or around UEcine that comes from someone else, and the terms it
comes under.

## What ships inside the plugin

No third-party source code, library, model or font ships inside UEcine. Everything under
`Source/`, `Shaders/` and `Resources/` was written for it.

The camera and lens database (`Resources/Database/*.json`) holds published facts about
equipment: sensor sizes, recording formats, focal lengths, T-stops, image circles. They were
compiled from manufacturers' own published specifications. Manufacturer and product names in it
are the trademarks of their owners and are used only to say which equipment a figure describes.
UEcine is not made, endorsed or sponsored by any camera or lens manufacturer.

## Unreal Engine plugins UEcine builds on

These ship with Unreal Engine and are used under the Unreal Engine EULA. UEcine turns them on;
it does not contain them.

| Engine plugin | Used for |
|---|---|
| Level Sequence Editor, Sequencer Scripting | Baking moves and focus pulls, the shot list |
| Movie Render Pipeline (Movie Render Graph), Mask Render Pass | Review, VFX and Master deliveries |
| Apple ProRes Media | ProRes review and master movies |
| NNE Runtime ORT (ONNX Runtime) | Running the optional AI models in the editor |

Apple ProRes is a trademark of Apple Inc. ONNX Runtime is distributed by Epic Games with the
engine under the MIT licence.

## Optional AI models (downloaded by you, not shipped)

UEcine contains no model weights. The AI tab offers these models, shows each one's licence, and
downloads a file from its makers' page only when you press Download. By downloading a model you
take it under its makers' licence. The licence named here is the one each model's page states;
read the page before you rely on it for commercial work.

| Model | Used for | Licence stated | Model page |
|---|---|---|---|
| Depth Anything V2 Small | Depth map of a still or plate | Apache-2.0 | https://huggingface.co/depth-anything/Depth-Anything-V2-Small |
| BiRefNet Lite | Matte of a still or plate | MIT | https://huggingface.co/ZhengPeng7/BiRefNet_lite |
| MoGe-2 Small | Lens estimate from a still | MIT | https://huggingface.co/Ruicheng/moge-2-vits-normal |

The files are fetched from `huggingface.co` in ONNX form (`onnx-community/depth-anything-v2-small`,
`onnx-community/BiRefNet_lite-ONNX`, `Ruicheng/moge-2-vits-normal-onnx`). Each download is
checked against a known SHA-256 before it is used.

## Optional helper program (uses your own Python)

`Resources/AI/uecine_ai_server.py` is part of UEcine. When you choose "Helper on this machine"
or run it on a server of your own, it runs in a Python you installed and uses packages you
installed. UEcine ships none of them.

| Package | Licence |
|---|---|
| onnxruntime | MIT |
| numpy | BSD 3-Clause |
| Pillow | MIT-CMU (HPND) |

## Optional online service (uses your own key)

The shot assistant sends a request to the Claude API (Anthropic) only after you have entered
your own API key and agreed in the AI tab. Use of that service is under your agreement with
Anthropic and is billed to your key. UEcine contains no Anthropic code. Claude is a trademark of
Anthropic, PBC.
