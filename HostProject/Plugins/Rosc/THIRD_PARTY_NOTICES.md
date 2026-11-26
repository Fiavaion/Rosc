# Rosc — third-party notices

Rosc itself is free and open source under the Apache License 2.0 (`LICENSE`). This file lists
everything in or around Rosc that comes from someone else, and the terms it comes under.

## What ships inside the plugin

No third-party source code, library, model or font ships inside Rosc. Everything under
`Source/`, `Shaders/` and `Resources/` was written for it.

The camera and lens database (`Resources/Database/*.json`) holds published facts about
equipment: sensor sizes, recording formats, focal lengths, T-stops, image circles. They were
compiled from manufacturers' own published specifications. Manufacturer and product names in it
are the trademarks of their owners and are used only to say which equipment a figure describes.
Rosc is not made, endorsed or sponsored by any camera or lens manufacturer.

The sample stage (`Content/Sample/RoscSampleStage`) is built from shapes, a floor and a sky
that ship with Unreal Engine. It refers to them; it does not contain them. The pictures in the
user guide (`Docs/`) are of Rosc and of that stage.

## Unreal Engine plugins Rosc builds on

These ship with Unreal Engine and are used under the Unreal Engine EULA. Rosc turns them on;
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

Rosc contains no model weights. The AI tab offers these models, shows each one's licence, and
downloads a file from its makers' page only when you press Download and, the first time, agree
to what it says of the licence and the training data. By downloading a model you
take it under its makers' licence. The licence named here is the one each model's page states;
read the page before you rely on it for commercial work.

| Model | Used for | Licence stated | Model page |
|---|---|---|---|
| Depth Anything V2 Small | Depth map of a still or plate | Apache-2.0 | https://huggingface.co/depth-anything/Depth-Anything-V2-Small |
| BiRefNet Lite | Matte of a still or plate | MIT | https://huggingface.co/ZhengPeng7/BiRefNet_lite |
| MoGe-2 Small | Lens estimate from a still | MIT | https://huggingface.co/Ruicheng/moge-2-vits-normal |

The files are fetched from `huggingface.co` in ONNX form (`onnx-community/depth-anything-v2-small`,
`onnx-community/BiRefNet_lite-ONNX`, `Ruicheng/moge-2-vits-normal-onnx`). Each download is
checked against a known SHA-256 before it is used. The MoGe-2 ONNX copy is published by the
model's first author; its page states no licence, and the original model is MIT.

### Training data

Each model learnt from data that others made and own; the owners are named here. Some of them
publish their data for research or other non-commercial use only. The licence of a model's
weights does not say whether those terms reach what a user makes with the model, and the law has
not settled it. Before a model's first download the AI tab shows who holds its licence and which
of its data is non-commercial, with the owners, and asks the user to agree (ADR-012). The tables
are what the models' makers and the data's owners publish, as found on 2026-11-26. They are not
legal advice.

### Depth Anything V2 Small

Licence Apache-2.0, held by The Depth Anything authors (The University of Hong Kong and TikTok) (https://github.com/DepthAnything/Depth-Anything-V2). Only the Small size is under Apache-2.0; the larger sizes are for non-commercial use, and Rosc does not offer them.

| Data | Owner | Terms | Owner allows commercial use |
|---|---|---|---|
| [BlendedMVS](https://github.com/YoYo000/BlendedMVS) | Yao Yao et al. (authors) | CC BY 4.0 | Yes |
| [Hypersim](https://github.com/apple/ml-hypersim) | Apple | CC BY-SA 3.0; the full scenes need purchased Evermotion assets | Yes |
| [IRS](https://github.com/HKBU-HPML/IRS) | Hong Kong Baptist University | None found for the data; the code is Apache-2.0 | Not clear |
| [TartanAir](https://theairlab.org/tartanair-dataset/) | Carnegie Mellon University (AirLab) | CC BY 4.0 | Yes |
| [Virtual KITTI 2](https://europe.naverlabs.com/research/computer-vision/proxy-virtual-worlds-vkitti-2/) | Naver Corporation | CC BY-NC-SA 3.0 | No |
| [BDD100K](https://doc.bdd100k.com/license.html) | The Regents of the University of California | BDD100K licence: commercial use for members of the BAIR Open Research Commons only | No |
| [Google Landmarks v2](https://github.com/cvdfoundation/google-landmark) | Google (labels); each photo's own author | CC BY 4.0 for the labels; each photo under its own licence | Not clear |
| [ImageNet-21K](https://image-net.org/download.php) | Princeton University and Stanford University | ImageNet terms of access: non-commercial research and education | No |
| [LSUN](https://github.com/fyu/lsun) | Fisher Yu, Yinda Zhang, Shuran Song, Ari Seff and Jianxiong Xiao (authors) | None found; the pictures were gathered from the web | Not clear |
| [Objects365](https://www.objects365.org/download.html) | Objects365 Consortium | For academic purposes only; labels CC BY 4.0 | No |
| [Open Images V7](https://storage.googleapis.com/openimages/web/factsfigures_v7.html) | Google | CC BY 4.0 for the labels; the photos are listed as CC BY 2.0 | Yes |
| [Places365](http://places2.csail.mit.edu/download-private.html) | Massachusetts Institute of Technology (CSAIL) | MIT terms of use: non-commercial research and education | No |
| [SA-1B](https://ai.meta.com/datasets/segment-anything/) | Meta | SA-1B Dataset Research License: research only | No |
| [DINOv2 (the pretrained backbone)](https://github.com/facebookresearch/dinov2) | Meta | Apache-2.0 for the code and weights | Yes |

### BiRefNet Lite

Licence MIT, held by Peng Zheng (https://github.com/ZhengPeng7/BiRefNet/blob/main/LICENSE).

| Data | Owner | Terms | Owner allows commercial use |
|---|---|---|---|
| [DIS5K](https://raw.githubusercontent.com/xuebinqin/DIS/main/DIS5K-Dataset-Terms-of-Use.pdf) | Xuebin Qin et al. (authors) | DIS5K terms of use: non-commercial research and education | No |
| [DUTS](https://saliencydetection.net/duts/) | Its authors (Dalian University of Technology) | All rights reserved by the authors; no licence stated | Not clear |
| [HRSOD](https://github.com/yi94code/HRSOD) | Yi Zeng et al. (authors) | None found | Not clear |
| [UHRSD](https://github.com/iCVTEAM/PGNet) | Chenxi Xie et al. (PGNet authors) | None found for the data | Not clear |
| [HRS10K](https://github.com/DrowsyMon/RMFormer) | The RMFormer authors (Dalian University of Technology) | None found for the data | Not clear |
| [P3M-10k](https://jizhizili.github.io/files/p3m_dataset_agreement/P3M-10k_Dataset_Release_Agreement.pdf) | Jizhizi Li, Sihan Ma, Jing Zhang and Dacheng Tao (The University of Sydney) | P3M-10k release agreement: MIT; the photos stay their owners' | Yes |
| [Humans](https://huggingface.co/datasets/schirrmacher/humans) | Marvin Schirrmacher | Apache-2.0 by its tag; made with image models whose terms it does not state | Not clear |
| [Swin Transformer (the pretrained backbone)](https://github.com/microsoft/Swin-Transformer/blob/main/LICENSE) | Microsoft Corporation | MIT; pretrained on ImageNet-22K, whose terms are non-commercial | Not clear |

### MoGe-2 Small

Licence MIT, held by Microsoft Corporation (https://github.com/microsoft/MoGe/blob/main/LICENSE). Rosc downloads an ONNX copy published by the model's first author, whose page states no licence; the original model is MIT.

| Data | Owner | Terms | Owner allows commercial use |
|---|---|---|---|
| [A2D2](https://registry.opendata.aws/aev-a2d2/) | Audi Electronics Venture GmbH | CC BY-ND 4.0 | Yes |
| [Argoverse 2](https://www.argoverse.org/about.html) | Argo AI, LLC | CC BY-NC-SA 4.0 | No |
| [ARKitScenes](https://github.com/apple/ARKitScenes) | Apple | Apple's own licence: non-commercial use, with commercial terms tied to monthly users | Not clear |
| [BlendedMVS](https://github.com/YoYo000/BlendedMVS) | Yao Yao et al. (authors) | CC BY 4.0 | Yes |
| [MegaDepth](https://www.cs.cornell.edu/projects/megadepth/) | Zhengqi Li and Noah Snavely (Cornell University) | CC BY 4.0 for the depth; each photo under its own licence | Not clear |
| [ScanNet++](https://scannetpp.mlsg.cit.tum.de/scannetpp/static/scannetpp-terms-of-use.pdf) | Technical University of Munich | ScanNet++ terms of use: commercial use strictly prohibited | No |
| [Taskonomy](https://raw.githubusercontent.com/StanfordVL/taskonomy/master/data/LICENSE) | Stanford University (the Taskonomy authors) | Taskonomy licence: non-commercial research and education | No |
| [Waymo Open Dataset](https://waymo.com/open/terms/) | Waymo LLC | Waymo licence for non-commercial use, which also bars commercial use of models trained on it | No |
| [ApolloScape Synthetic](https://developer.apollo.auto/synthetic.html) | Baidu (Apollo) | Apollo terms of use: personal, non-commercial research or teaching | No |
| [EDEN](https://lhoangan.github.io/eden/) | University of Amsterdam (authors) | None found | Not clear |
| [GTA-SfM](https://github.com/HKUST-Aerial-Robotics/Flow-Motion-Depth) | Kaixuan Wang and Shaojie Shen (HKUST Aerial Robotics) | None found; rendered in a commercial game | Not clear |
| [Hypersim](https://github.com/apple/ml-hypersim) | Apple | CC BY-SA 3.0; the full scenes need purchased Evermotion assets | Yes |
| [IRS](https://github.com/HKBU-HPML/IRS) | Hong Kong Baptist University | None found for the data; the code is Apache-2.0 | Not clear |
| [3D Ken Burns](https://github.com/sniklaus/3d-ken-burns) | Adobe Research | CC BY-NC-SA 4.0 | No |
| [MatrixCity](https://huggingface.co/datasets/BoDai/MatrixCity) | The MatrixCity authors (CUHK and Shanghai AI Laboratory) | CC BY-NC 4.0 on its dataset page (its code page says Apache-2.0) | No |
| [Mid-Air](https://midair.ulg.ac.be/) | University of Liège (Montefiore Institute) | CC BY-NC-SA 4.0 | No |
| [MVS-Synth](https://huggingface.co/datasets/phuang17/MVS-Synth) | Po-Han Huang et al. (authors) | For research and education only | No |
| [Structured3D](https://structured3d-dataset.org/) | Kujiale.com, ShanghaiTech University and Penn State University | Structured3D terms of use: non-commercial research and education | No |
| [SYNTHIA](https://synthia-dataset.net/terms-of-use/) | Computer Vision Center and Universitat Autònoma de Barcelona | CC BY-NC-SA 3.0 | No |
| [Synscapes](https://synscapes.on.liu.se/download.html) | 7D Labs, Inc. | Synscapes licence: non-commercial only | No |
| [UnrealStereo4K](https://huggingface.co/datasets/fabiotosi92/UnrealStereo4K) | Fabio Tosi, Yiyi Liao, Carolin Schmitt and Andreas Geiger (authors) | MIT by its tag; the original states no terms | Not clear |
| [TartanAir](https://theairlab.org/tartanair-dataset/) | Carnegie Mellon University (AirLab) | CC BY 4.0 | Yes |
| [UrbanSyn](https://www.urbansyn.org/) | Computer Vision Center (UAB) and CITIC (University of A Coruña) | CC BY-SA 4.0, for research and commercial purposes | Yes |
| [Objaverse 1.0](https://huggingface.co/datasets/allenai/objaverse) | Allen Institute for AI | ODC-By 1.0 overall; each object under its own licence, some non-commercial | Not clear |
| [DINOv2 (the pretrained backbone)](https://github.com/facebookresearch/dinov2) | Meta | Apache-2.0 for the code and weights | Yes |

## Optional helper program (uses your own Python)

`Resources/AI/rosc_ai_server.py` is part of Rosc. When you choose "Helper on this machine"
or run it on a server of your own, it runs in a Python you installed and uses packages you
installed. Rosc ships none of them.

| Package | Licence |
|---|---|
| onnxruntime | MIT |
| numpy | BSD 3-Clause |
| Pillow | MIT-CMU (HPND) |

## Optional online service (uses your own key)

The shot assistant sends a request to the Claude API (Anthropic) only after you have entered
your own API key and agreed in the AI tab. Use of that service is under your agreement with
Anthropic and is billed to your key. Rosc contains no Anthropic code. Claude is a trademark of
Anthropic, PBC.

## Trademarks

Unreal and Unreal Engine are trademarks or registered trademarks of Epic Games, Inc. in the
United States of America and elsewhere. Steadicam is a registered trademark of The Tiffen
Company; Rosc uses the word for the kind of support its Steadicam rig models. YouTube is a
trademark of Google LLC; Rosc's YouTube delivery makes a file to the settings YouTube publishes,
and does not upload it. Rosc is not made, endorsed or sponsored by any of them.
