# Full Scene Shader Pipeline V2 Material Evidence

Status: `BLOCKED`

## Summary

- `asset_count`: `33`
- `v2_material_ready_asset_count`: `1`
- `v2_material_ready_asset_ratio`: `0.0303`
- `pbr_texture_ready_asset_count`: `1`
- `missing_hero_texture_evidence_count`: `10`
- `unknown_material_family_asset_count`: `0`
- `primitive_hero_material_blocker_count`: `24`
- `runtime_policy_bridge_asset_count`: `33`
- `scene_count`: `5`

## Material Families

- `dielectric`: `11`
- `fabric`: `1`
- `metal`: `13`
- `painted_wall`: `1`
- `plastic`: `4`
- `tile`: `1`
- `wood`: `8`

## Runtime Policy Bridge

### scene_material_class
- `CeramicTile`: `1`
- `Default`: `11`
- `Plastic`: `2`
- `PolishedMetal`: `12`
- `PolishedWood`: `7`
### reflection_preference
- `LocalProbe`: `9`
- `NeutralFallback`: `11`
- `RTReflection`: `12`
- `SSR`: `1`
### temporal_policy
- `StableDiffuse`: `20`
- `StableGlossy`: `13`
### post_sensitivity
- `ExposureProtected`: `13`
- `Normal`: `20`

## Scenes

| Scene | Objects | Registry Bound | Hero Registry Bound | Primitive Hero Blockers | Missing Registry Material |
|---|---:|---:|---:|---:|---:|
| home_kitchen_lantern | 135 | 27 | 4 | 2 | 2 |
| home_office_evening | 131 | 24 | 6 | 8 | 8 |
| basketball_gym_day | 125 | 30 | 0 | 7 | 7 |
| neon_streamer_concert | 154 | 39 | 21 | 7 | 7 |
| rt_showcase_gallery | 0 | 0 | 0 | 0 | 0 |

## Top Blocked Assets

- `bench`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `bookcaseopen`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `books`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `chair`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `chairdesk`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `chairmoderncushion`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `computerkeyboard`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `computerscreen`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `desk`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `hoodmodern`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchen_faucet_gooseneck`: engine proxy mesh; useful detail but not AAA final asset; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `kitchencabinetupper`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchencabinetupperdouble`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchencabinetupperlow`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchencoffeemachine`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchenfridge`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `kitchenmicrowave`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `kitchensink`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
- `kitchenstove`: missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model; missing collision evidence for V2 final asset
- `lamproundtable`: hero surface lacks PBR texture readiness; missing LOD chain; missing LOD evidence for V2 final asset; missing PBR texture evidence for V2 material model
