# Rig presets: what the figures are and where they come from

The Grip tab offers camera supports by name ("Telescoping crane, 30 ft"). Each one is a preset:
a rig type plus the range, top speed and greatest acceleration of each of its axes. The move
checker compares a blocked move with these figures and warns when the move asks for more.

## What the figures are

The figures describe a **class of equipment as a competent crew works it**, not a product. They
are rounded, deliberately a little conservative, and chosen so that a move the checker accepts
is one a crew would agree to do. They are **not** taken from any manufacturer's data sheet, and
no preset carries a manufacturer or product name.

A warning never blocks anything. It says what the move asks for, what the support manages, and
what would bring the move within reach (usually: give it more time). A user who wants a move no
real support could make can ignore the warning, or take the camera off its support.

Distances below are in metres and speeds in metres per second; the code stores centimetres.
"Fixed" means the axis is set when the support is built and cannot change during a shot.

## Heads

| Head | Pan | Tilt | Roll |
|---|---|---|---|
| Fluid head (tripod, slider, dolly, jib) | ±720°, 120°/s, 400°/s² | ±90°, 90°/s, 300°/s² | none |
| Remote head (crane) | ±720°, 120°/s, 300°/s² | ±180°, 120°/s, 300°/s² | ±180°, 90°/s, 200°/s² |

## Supports

| Preset (id) | Axis | Range | Top speed | Acceleration |
|---|---|---|---|---|
| Tripod, fluid head (`tripod-fluid-head`) | Height | 0.6–1.7 m | fixed | |
| Slider, 1 m (`slider-100`) | Slide | 0–1 m | 0.5 m/s | 1 m/s² |
| | Height | 0.3–1.7 m | fixed | |
| Slider, 1.5 m (`slider-150`) | Slide | 0–1.5 m | 0.5 m/s | 1 m/s² |
| | Height | 0.3–1.7 m | fixed | |
| Dolly on track (`dolly`) | Track | the track laid (6 m by default) | 2.5 m/s | 1.5 m/s² |
| | Column | 0.4–1.9 m | 0.3 m/s | 0.6 m/s² |
| Jib, 2 m arm (`jib-2m`) | Arm length | 2 m | fixed | |
| | Pivot height | 1–1.8 m | fixed | |
| | Swing | ±360° | 45°/s | 60°/s² |
| | Boom | −40° to +50° | 30°/s | 45°/s² |
| Jib, 4 m arm (`jib-4m`) | as the 2 m jib, with a 4 m arm | | | |
| Telescoping crane, 22 ft (`crane-22ft`) | Telescope | 2–6.7 m | 1.8 m/s | 2 m/s² |
| Telescoping crane, 30 ft (`crane-30ft`) | Telescope | 2.8–9.1 m | 1.8 m/s | 2 m/s² |
| Telescoping crane, 50 ft (`crane-50ft`) | Telescope | 4.6–15.2 m | 1.8 m/s | 2 m/s² |
| All three cranes | Pivot height | 1.5–2.5 m | fixed | |
| | Swing | ±360° | 40°/s | 40°/s² |
| | Boom | −30° to +50° | 25°/s | 30°/s² |

## Free-moving supports

These go where the operator or pilot takes them, so forward and sideways travel have no range,
only a speed and an acceleration.

| Preset (id) | Forward and sideways | Height | Pan | Tilt | Roll |
|---|---|---|---|---|---|
| Steadicam (`steadicam`) | 3 m/s, 2 m/s² | 0.6–2 m, 0.5 m/s | ±720°, 120°/s | ±60°, 60°/s | ±15°, 30°/s |
| Handheld (`handheld`) | 4 m/s, 3 m/s² | 0.2–2.1 m, 1 m/s | ±720°, 180°/s | ±90°, 120°/s | ±45°, 60°/s |
| Drone (`drone`) | 15 m/s, 6 m/s² | 0–120 m, 5 m/s | ±720°, 100°/s | −90° to +30°, 60°/s | ±30°, 30°/s |

## Operator movement

The operator setting adds a small, repeatable movement on top of the blocked move when it is
baked: breathing on a fluid head, arm sway on a remote head, float on a Steadicam, footfall on
the shoulder, wind on a drone. It is a sum of slow sine waves seeded by the take number, so the
same take always gives the same movement. The sizes are judged by eye, not measured from real
operators.

## Changing the figures

The presets live in `UEcineCore/Private/UEcineRigTypes.cpp`. A preset's id is saved in levels,
so ids are not renamed or removed once a release has shipped; figures can be corrected at any
time, and this document is updated in the same change.
