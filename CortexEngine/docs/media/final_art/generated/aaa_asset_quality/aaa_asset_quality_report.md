# AAA Asset Quality Report

Status: `BLOCKED`
Scene count: `5`
Passed scenes: `0`
Blocked scenes: `5`

## Summary

| Scene | Status | Score | Runtime Meshes | Bound Objects | Primitive Hero Blockers | Unique Assets | Primitive Ratio | PBR Ratio | LOD Ratio | Collision Ratio |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| home_kitchen_lantern | BLOCKED | 0.6014 | 27 | 27 | 2 | 19 | 0.7556 | 0.0370 | 0.0000 | 0.0000 |
| home_office_evening | BLOCKED | 0.5774 | 24 | 24 | 8 | 14 | 0.7786 | 0.0000 | 0.0000 | 0.0000 |
| basketball_gym_day | BLOCKED | 0.4827 | 30 | 30 | 7 | 4 | 0.6240 | 0.0000 | 0.0000 | 0.0000 |
| neon_streamer_concert | BLOCKED | 0.5658 | 39 | 39 | 7 | 6 | 0.7143 | 0.0000 | 0.0000 | 0.0000 |
| rt_showcase_gallery | BLOCKED | 0.1000 | 0 | 0 | 0 | 0 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |

## Blockers

### home_kitchen_lantern
- collision readiness ratio below minimum
- hero role still primitive/blockout
- lod readiness ratio below minimum
- pbr texture ratio below minimum
- required role coverage below minimum
- primitive hero roles: cabinet, countertop
- missing required roles: kitchen_wall, tile_floor

### home_office_evening
- collision readiness ratio below minimum
- hero role still primitive/blockout
- lod readiness ratio below minimum
- pbr texture ratio below minimum
- primitive hero roles: book, keyboard, monitor, shelf

### basketball_gym_day
- collision readiness ratio below minimum
- hero role still primitive/blockout
- lod readiness ratio below minimum
- pbr texture ratio below minimum
- required role coverage below minimum
- unique runtime asset count below minimum
- primitive hero roles: backboard, ball, bleacher, hoop, scoreboard
- missing required roles: ceiling_light, stadium_seat

### neon_streamer_concert
- collision readiness ratio below minimum
- hero role still primitive/blockout
- lod readiness ratio below minimum
- pbr texture ratio below minimum
- required role coverage below minimum
- unique runtime asset count below minimum
- primitive hero roles: hero_screen, stage, stage_light
- missing required roles: audience_riser, ceiling_plane, desk, overhead_light, venue_floor, venue_wall

### rt_showcase_gallery
- collision readiness ratio below minimum
- lod readiness ratio below minimum
- pbr texture ratio below minimum
- provenance readiness ratio below minimum
- required role coverage below minimum
- runtime mesh instance count below minimum
- unique runtime asset count below minimum
- missing required roles: hero_liquid_pair, plinth, reflection_card, spill_contact, vessel_rim

