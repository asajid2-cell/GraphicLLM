# Full Scene Shader Pipeline V2 Material Upgrade Work Orders

Status: `READY`

## Summary

- `work_order_count`: `56`
- `p0_count`: `34`
- `p1_count`: `22`
- `primitive_hero_material_order_count`: `24`
- `hero_asset_material_order_count`: `10`
- `registry_asset_material_order_count`: `22`

## Priority Counts

- `P0`: `34`
- `P1`: `22`

## Top Orders

### shader_material__upgrade_hero_asset_material_evidence__chair
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `chair`
- hero refs: `21`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_hero_asset_material_evidence__books
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `books`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, cc0_curated_library, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__chairdesk
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `chairdesk`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_hero_asset_material_evidence__computerkeyboard
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `computerkeyboard`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__computerscreen
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `computerscreen`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, cc0_curated_library, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__desk
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `desk`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_hero_asset_material_evidence__kitchenfridge
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `kitchenfridge`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__kitchensink
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `kitchensink`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__lamproundtable
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `lamproundtable`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, cc0_curated_library, trellis_image_large

### shader_material__upgrade_hero_asset_material_evidence__table
- priority: `P0`
- kind: `upgrade_hero_asset_material_evidence`
- asset: `table`
- hero refs: `1`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; hero-surface material review packet
- providers: artist_authored_pbr, cc0_curated_library, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_backboard
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_ball
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_bleacher_row_0
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_bleacher_row_1
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_bleacher_row_2
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_rim
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### basketball_gym_day__primitive_hero_material__sak_gym_scoreboard
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `basketball_gym_day`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_kitchen_lantern__primitive_hero_material__sak_kitchen_countertop
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_kitchen_lantern`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_kitchen_lantern__primitive_hero_material__sak_kitchen_lower_cabinets
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_kitchen_lantern`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_book_0
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_book_1
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_book_2
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_keyboard
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_monitor
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_shelf_0
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_shelf_1
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### home_office_evening__primitive_hero_material__sak_office_shelf_2
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `home_office_evening`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_floor_light_0
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_floor_light_1
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_floor_light_2
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_floor_light_3
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_floor_light_4
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_screen
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### neon_streamer_concert__primitive_hero_material__sak_concert_stage
- priority: `P0`
- kind: `replace_primitive_hero_material_surface`
- scene: `neon_streamer_concert`
- reason: hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2
- needed: registry-backed replacement asset or admitted architecture material kit; complete PBR texture set; scale/support/contact anchors; LOD chain and collision proxy if object remains mesh-backed
- providers: artist_authored_pbr, cc0_curated_library, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_registry_asset_material_evidence__bench
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `bench`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_registry_asset_material_evidence__bookcaseopen
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `bookcaseopen`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_registry_asset_material_evidence__chairmoderncushion
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `chairmoderncushion`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1

### shader_material__upgrade_registry_asset_material_evidence__hoodmodern
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `hoodmodern`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2
- providers: artist_authored_pbr, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_registry_asset_material_evidence__kitchen_faucet_gooseneck
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `kitchen_faucet_gooseneck`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2; preview image and visual review evidence
- providers: artist_authored_pbr, hunyuan3d_2_1, trellis_image_large

### shader_material__upgrade_registry_asset_material_evidence__kitchencabinetupper
- priority: `P1`
- kind: `upgrade_registry_asset_material_evidence`
- asset: `kitchencabinetupper`
- hero refs: `0`
- reason: registered asset lacks the material evidence required by Full Scene Shader Pipeline V2
- needed: complete PBR texture set: base color, normal, ORM/roughness/metallic/AO; LOD0/LOD1/LOD2 chain registered in Asset Registry V2; collision proxy registered in Asset Registry V2
- providers: cc0_curated_library, artist_authored_pbr, hunyuan3d_2_1
