# Camera and lens database: sources and open items

The shipped data in `HostProject/Plugins/Rosc/Resources/Database/` was checked against
manufacturer documents on 2026-11-11; the additions of 2026-11-26 are marked below. Rule: a value that cannot be traced to a source is left
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
| ZEISS Supreme Prime | ZEISS Supreme Prime brochure; 15, 18, 40 and 200 mm and the 135 mm's 1.4 m close focus from the current ZEISS Supreme brochure "Technical Details" and product page (2026-11-26; the 200 mm is T2.2, which replaced the older brochure's T2.1); 46.2 mm image circle from the older ZEISS brochure; 16 blades (secondary) |
| ZEISS CP.3 | zeiss.com CP.3 product page |
| Angénieux Optimo | Angénieux brochure and Film and Digital Times, December 2022 |
| Angénieux Optimo 19.5-94 and 28-340 | Angénieux catalogue 2014: S35+ 31.4 mm coverage, T2.6 and T3.2, close focus 0.62 m and 1.23 m (2026-11-26). The minimum aperture is not published there, so the database's T22 default applies |
| SIGMA FF High Speed Prime | sigma-global.com, one page per lens: T-stops, close focus, 43.3 mm image circle, 9 blades, T16 (2026-11-26). Where close focus is measured from is not stated |
| Film gauges | SMPTE camera aperture image standards, nominal figures (2026-11-26): Academy 22.0 × 16.0 (ST 59, Style A), Standard 16 10.26 × 7.49 (ST 7), Super 16 12.35 × 7.42 (201M, Type W), 65mm 5-perf 52.48 × 23.01 (ST 215), IMAX 15/70 70.41 × 51.61 (417M) |

## Kept on general knowledge, no document retrieved in the check

- Anamorphic scope, 21.95 × 18.59. The other film gauges were set to the SMPTE figures on
  2026-11-26, replacing figures from secondary sources (Academy 21.95, Super 16 12.52 × 7.41,
  65mm 5-perf 52.63, which is the 8/70 width, and IMAX height 52.63). Kodak's format
  infographic gives capture areas, not camera apertures, so it is not used.
- Base ISO for ALEXA 35, ALEXA Mini, BURANO, V-RAPTOR and FX3; lens mounts for VENICE 2,
  BURANO and V-RAPTOR VV.
- Sony FX6 and FX3 sensor areas (FX3 16:9 height and FX6 UHD width are derived).
- ARRI Signature Prime image circle is set to 44.7 mm, the ALEXA LF open gate diagonal the
  line is designed to cover; published figures range from 44.7 to 46 mm.
- ZEISS CP.3 image circle is set to the full-frame diagonal, 43.3 mm.

## Left out because they could not be verified

- Atlas Orion (the maker's page could not be read; close focus figures disagree between
  secondary sources, which do not say where they measure from).
- Iris blade counts for CP.3 and the Optimo zooms (only retailers and a community wiki give
  them, and they disagree).

## Naming notes

- ARRI also sells the ALEXA 35 Xtreme with the same sensor modes.
- RED's current names are V-RAPTOR [X] 8K VV and KOMODO-X; the sensor areas are unchanged.
- Cooke S4/i is reported discontinued from 2025, succeeded by S4 FF/i; it remains common on
  rental shelves.
