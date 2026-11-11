# Camera and lens database: sources and open items

The shipped data in `HostProject/Plugins/UEcine/Resources/Database/` was checked against
manufacturer documents on 2026-11-11. Rule: a value that cannot be traced to a source is left
out, not guessed. "Derived" means a size calculated from the published photosite pitch.

## Sources

| Entry | Source |
|---|---|
| ARRI ALEXA 35 formats | ARRI recording formats poster and ALEXA 35 brochure (arri.com resource 400322; afcinema.com ALEXA 35 brochure) |
| ARRI ALEXA Mini LF | ARRI Mini LF technical data sheet (arri.com resource 261224) |
| ARRI ALEXA Mini | ARRI recording areas and framelines SUP 4.0 (arri.com resource 178064) |
| ARRI ALEXA 65 | arrirental.com/en/cameras/alexa-65; 5.1K and 4.3K crop sizes derived (8.25 µm) |
| Sony VENICE 2 8K | Sony VENICE 2 recording formats sheet |
| Sony BURANO | Sony BURANO recording formats sheet (R5) |
| RED V-RAPTOR 8K VV, 8K S35, KOMODO 6K | RED operation guides, "All Formats" and tech specs (docs.red.com); S35 6K derived |
| Blackmagic URSA Cine 12K LF, Cinema Camera 6K, Pocket 6K G2 | blackmagicdesign.com tech specs; cropped sizes derived |
| Canon EOS C500 Mark II, C70 | Canon specification pages |
| ARRI Signature Prime | T-stops and close focus: Keslow Camera listing (secondary); 11 blades (secondary) |
| ARRI Master Anamorphic | ARRI data sheets and product leaflet |
| Cooke S4/i | Cooke specification sheet 030623 (millimetre figures used) |
| Cooke Anamorphic/i | Cooke Anamorphic/i S35 specification 110222; 11 blades (secondary) |
| ZEISS Supreme Prime | ZEISS Supreme Prime brochure; 16 blades (secondary) |
| ZEISS CP.3 | zeiss.com CP.3 product page |
| Angénieux Optimo | Angénieux brochure and Film and Digital Times, December 2022 |

## Kept on general knowledge, no document retrieved in the check

- Film gauges: SMPTE camera apertures converted from their inch definitions (Academy
  21.95 × 16.00, scope 21.95 × 18.59, Super 16 12.52 × 7.41, Standard 16 10.26 × 7.49,
  65mm 5-perf 52.63 × 23.01, IMAX 15-perf 70.41 × 52.63). Kodak's format infographic prints
  different figures for Super 16 (12.42 × 7.44), 65mm (52.15 × 23.07) and IMAX
  (70.41 × 56.62). Confirm against the SMPTE documents before release.
- Base ISO for ALEXA 35, ALEXA Mini, BURANO, V-RAPTOR and FX3; lens mounts for VENICE 2,
  BURANO and V-RAPTOR VV.
- Sony FX6 and FX3 sensor areas (FX3 16:9 height and FX6 UHD width are derived).
- ARRI Signature Prime image circle is set to 44.7 mm, the ALEXA LF open gate diagonal the
  line is designed to cover; published figures range from 44.7 to 46 mm.
- ZEISS CP.3 image circle is set to the full-frame diagonal, 43.3 mm.

## Left out because they could not be verified

- ZEISS Supreme Prime 15, 18, 40 and 200 mm (close focus not confirmed; 200 mm is T2.1).
- Sigma Cine FF High Speed (close focus confirmed for only five of ten lenses).
- Atlas Orion (close focus figures disagree between sources; 25 mm not confirmed).
- Angénieux Optimo 19.5-94 and 28-340 (image circle not confirmed).
- Iris blade counts for CP.3 and the Optimo zooms.

## Naming notes

- ARRI also sells the ALEXA 35 Xtreme with the same sensor modes.
- RED's current names are V-RAPTOR [X] 8K VV and KOMODO-X; the sensor areas are unchanged.
- Cooke S4/i is reported discontinued from 2025, succeeded by S4 FF/i; it remains common on
  rental shelves.
