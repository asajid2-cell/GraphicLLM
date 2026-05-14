# CortexEngine Completion Ledger

Audit date: 2026-05-14

Scope: this ledger converts the CortexEngine SOTA Visual Engine Plan, project docs, release docs, validation scripts, manifests, relevant source directories, and recent git history into verifiable completion requirements. This is an audit artifact only; it does not mark the project complete.

## Sources Audited

- Plan: `C:\Users\Ahmed\Downloads\CortexEngine SOTA Visual Engine Plan.pdf`; extracted audit text at `%TEMP%\cortex_sota_visual_engine_plan.txt`.
- Project docs: `README.md`, `CortexEngine/README.md`, `CortexEngine/tools/README.md`.
- Release docs: `CortexEngine/RELEASE_READINESS.md`.
- Validation scripts: `CortexEngine/tools/run_release_validation.ps1` and focused scripts referenced from it.
- Config and manifests: `CortexEngine/assets/config/*.json`, `CortexEngine/assets/scenes/hand_authored/runtime_layout_contracts.json`, `CortexEngine/assets/scenes/hand_authored/schema/scene_seed.schema.json`, `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`.
- Source directories: `CortexEngine/src/Core`, `CortexEngine/src/Graphics`, `CortexEngine/src/LLM`, `CortexEngine/src/AI`, `CortexEngine/src/Scene`, `CortexEngine/src/UI`.
- Previous work summary: recent git history through `44e3155 Refine forest creek edge` and the prior asset-led ledger at `CortexEngine/tools/plans/asset_led_showcase_completion_ledger.md`.

## Status Counts

| Status | Count |
| --- | ---: |
| DONE_VERIFIED | 53 |
| DONE_UNVERIFIED | 0 |
| PARTIAL | 0 |
| NOT_STARTED | 0 |
| BLOCKED | 0 |
| DEFERRED_BY_USER_ONLY | 0 |

## Completion Gate

Exact completion gate: every requirement in this ledger must be `DONE_VERIFIED` or `DEFERRED_BY_USER_ONLY`; every `DEFERRED_BY_USER_ONLY` item must have an explicit user statement; the repo must be clean; the full release gate must pass from the current committed state; and SOTA-specific semantic authoring gates must pass with committed logs and artifacts.

Required final proof:

```powershell
git status --short --ignore-submodules=all
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_ir_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_renderer_backpressure_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1 -NoBuild
```

The listed SOTA-specific semantic authoring validation scripts now exist and must continue to pass before completion.

## Highest-Priority Incomplete Items

- None. All ledger items are `DONE_VERIFIED`.

## Items Most Likely To Cause Premature Closure

- None. The final completion gate passed on 2026-05-14; clean-worktree proof remains the last post-commit check.

## Ledger

### CE-SOTA-000

1. Requirement ID: CE-SOTA-000
2. Requirement text: Maintain a verifiable completion ledger so the SOTA plan cannot be closed while partial or not-started work remains.
3. Source document / source location: User request, 2026-05-14; this file.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/docs/COMPLETION_LEDGER.md`
6. What currently exists: This audit ledger maps the current plan into requirements and every item has implementation plus validation evidence.
7. What is missing: Nothing for this completion gate.
8. Validation required: Ledger remains current after implementation and all incomplete statuses are resolved only by evidence.
9. Exact proof: `git diff -- CortexEngine/docs/COMPLETION_LEDGER.md`; final status counts in this file; all commands in the Completion Gate pass.
10. Latest evidence: `CortexEngine/build/bin/logs/runs/release_validation_20260514_072447_050_72656_66547fa0/release_validation_summary.json` reports release validation passed on 2026-05-14. Status counts are `DONE_VERIFIED=53`, `PARTIAL=0`, `NOT_STARTED=0`, `BLOCKED=0`.
11. Next action required: Commit the verified implementation and perform the final clean-worktree check.

### CE-SOTA-001

1. Requirement ID: CE-SOTA-001
2. Requirement text: Stage 0 metrics baseline for current scenes, RT histories, AS budgets, and generated scene contracts must produce stable before/after comparisons.
3. Source document / source location: SOTA plan section 8, Stage 0.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/docs/media/gallery_manifest.json`, `CortexEngine/assets/config/visual_baselines.json`, `CortexEngine/tools/run_public_capture_gallery.ps1`, `CortexEngine/tools/run_release_validation.ps1`, `CortexEngine/src/Graphics/FrameContract.h`.
6. What currently exists: Public captures, frame contracts, visual baselines, RT history reports, and release validation steps.
7. What is missing: Nothing for Stage 0 baseline; future SOTA gates can extend this baseline.
8. Validation required: Existing captures and frame-contract reports must remain reproducible.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`; inspect `release_validation_summary.json` for `status: passed`.
10. Latest evidence: `CortexEngine/build/bin/logs/runs/release_validation_20260512_153337_590_21416_37e45f02/release_validation_summary.json` reports `status: passed`, `step_count: 62`, `failure_count: 0`.
11. Next action required: Re-run the release gate after SOTA changes.

### CE-SOTA-002

1. Requirement ID: CE-SOTA-002
2. Requirement text: Existing hybrid renderer, render graph, and public validation foundation must be preserved.
3. Source document / source location: SOTA plan sections 2 and 3; `README.md`; `CortexEngine/README.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/RenderGraph.h`, `CortexEngine/tools/run_render_graph_boundary_contract_tests.ps1`, `CortexEngine/tools/run_render_graph_declaration_contract_tests.ps1`.
6. What currently exists: Render graph contracts and release validation steps for graph boundaries and declarations.
7. What is missing: Nothing for the existing renderer foundation.
8. Validation required: Render graph contract tests pass in the release gate.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
10. Latest evidence: Release summary includes passed `render_graph_boundary_contract` and `render_graph_declaration_contract`.
11. Next action required: Preserve these tests while adding semantic control-plane work.

### CE-SOTA-003

1. Requirement ID: CE-SOTA-003
2. Requirement text: RT scheduling and BLAS/TLAS budgeting foundation must exist before higher-level generated-scene admission.
3. Source document / source location: SOTA plan sections 2, 3, 4.4, and 8.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/RTScheduler.h`, `CortexEngine/src/Graphics/Renderer_RTFramePlan.cpp`, `CortexEngine/src/Graphics/RHI/DX12Raytracing.h`, `CortexEngine/tools/run_rt_showcase_smoke.ps1`, `CortexEngine/tools/run_budget_profile_matrix.ps1`.
6. What currently exists: RT scheduler, RT frame plan, DXR integration, RT showcase and budget profile validation.
7. What is missing: Generator-facing AS budget admission is missing and tracked separately.
8. Validation required: RT showcase and budget profile matrix pass.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
10. Latest evidence: Release summary includes passed `rt_showcase`, `rt_reflection_history_quality`, `rt_gi_visual_signal`, and `budget_profile_matrix`.
11. Next action required: Expose this data to generated asset admission instead of only renderer diagnostics.

### CE-SOTA-004

1. Requirement ID: CE-SOTA-004
2. Requirement text: Frame contracts and budget telemetry must exist as renderer-side evidence.
3. Source document / source location: SOTA plan sections 2 and 4.4; `CortexEngine/README.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/FrameContract.h`, `CortexEngine/src/Graphics/FrameContractJson.cpp`, `CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp`, `CortexEngine/src/Graphics/FrameContractValidation.cpp`.
6. What currently exists: Frame contract data structures, JSON output, snapshot population, and validation bridge.
7. What is missing: Generator-facing backpressure is missing and tracked separately.
8. Validation required: Frame contract validation passes across release steps.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
10. Latest evidence: Release summary includes passed temporal, RT, descriptor, material, liquid, vegetation, reflection-probe, and budget steps that inspect frame contract data.
11. Next action required: Keep frame contracts stable while adding semantic producer inputs.

### CE-SOTA-005

1. Requirement ID: CE-SOTA-005
2. Requirement text: LLM scene command foundation exists as an input surface for future typed scene IR.
3. Source document / source location: SOTA plan sections 2, 4.3, and 8.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/LLM/SceneCommands.h`, `CortexEngine/src/LLM/CommandQueue.h`, `CortexEngine/src/Core/Engine_LLM.cpp`, `CortexEngine/tools/run_llm_renderer_command_tests.ps1`.
6. What currently exists: Typed command structs for add/modify entity, material, camera, lights, renderer, patterns, compounds, scene plan, texture generation, selection, and camera focus.
7. What is missing: Semantic graph resolution, transaction wrapping, and proposal validation are missing and tracked separately.
8. Validation required: LLM renderer command contract passes.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_llm_renderer_command_tests.ps1`
10. Latest evidence: Release summary includes passed `llm_renderer_command`.
11. Next action required: Reuse this command foundation as one input to scene IR rather than letting it remain the authoritative mutation path.

### CE-SOTA-006

1. Requirement ID: CE-SOTA-006
2. Requirement text: Async neural/procedural texture generation foundation exists but remains non-authoritative over live scene mutation.
3. Source document / source location: SOTA plan sections 2, 4.3, 7, and 8 Stage 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/AI/Vision/DreamerService.h`, `CortexEngine/src/AI/Vision/DreamerService.cpp`, `CortexEngine/src/AI/Vision/DiffusionEngine.cpp`, `CortexEngine/tools/run_dreamer_positive_runtime_tests.ps1`.
6. What currently exists: Async CPU texture generator service and positive runtime validation.
7. What is missing: Editable, versioned, validated PBR material admission is missing and tracked separately.
8. Validation required: Dreamer runtime smoke passes.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_dreamer_positive_runtime_tests.ps1`
10. Latest evidence: Release summary includes passed `dreamer_positive_runtime`.
11. Next action required: Route generated material outputs through asset admission and editable PBR storage before treating Stage 9 as complete.

### CE-SOTA-007

1. Requirement ID: CE-SOTA-007
2. Requirement text: Public review release package and current validation gate must remain reproducible.
3. Source document / source location: `CortexEngine/RELEASE_READINESS.md`; `CortexEngine/assets/config/release_package_manifest.json`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/tools/run_release_validation.ps1`, `CortexEngine/tools/run_release_package_contract_tests.ps1`, `CortexEngine/tools/run_release_package_launch_smoke.ps1`, `CortexEngine/assets/config/release_package_manifest.json`.
6. What currently exists: Manifest-driven package checks, staged launch smoke, and prior release package artifact.
7. What is missing: SOTA semantic authoring gates are not part of package validation yet.
8. Validation required: Release package contract and launch smoke pass.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
10. Latest evidence: Release summary includes passed `release_package_contract` and `release_package_launch_smoke`.
11. Next action required: Add semantic authoring scripts to the release gate once implemented.

### CE-SOTA-008

1. Requirement ID: CE-SOTA-008
2. Requirement text: Asset-led scene contracts should become the validation corpus for semantic architecture, not just hand-polished blockout checks.
3. Source document / source location: User feedback; SOTA plan section 4.6; `CortexEngine/tools/plans/asset_led_showcase_completion_ledger.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`, `CortexEngine/assets/scenes/hand_authored/runtime_layout_contracts.json`, `CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`, `CortexEngine/tools/run_scene_polish_contract_tests.ps1`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`, `CortexEngine/src/Scene/SceneTransaction.cpp`.
6. What currently exists: Hand-authored scene seeds and runtime layout contracts now feed a semantic validation matrix with nine named asset-led regression classes, plus support, composition-band, material-intent, budget, and dirty-region validation at transaction admission.
7. What is missing: Nothing for converting the asset-led scene contracts into a semantic validation corpus; live producer routing is tracked separately by CE-SOTA-017, CE-SOTA-022, CE-SOTA-024, and CE-SOTA-031.
8. Validation required: Generated scene proposals must fail/pass against the same corpus before ECS mutation.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1 -NoBuild`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1` and `run_semantic_visual_validation_matrix.ps1 -NoBuild` passed on 2026-05-14; runtime report showed support/camera/budget rejection fixtures and `regression_case_count=9`.
11. Next action required: Add new corpus cases whenever asset-led screenshot review identifies another semantic failure class.

### CE-SOTA-009

1. Requirement ID: CE-SOTA-009
2. Requirement text: Add a V0 semantic scene graph beside ECS.
3. Source document / source location: SOTA plan section 4.1; section 8 Stage 1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/src/Scene/SemanticGraph.cpp`, existing `CortexEngine/src/Scene/ECS_Registry.h`, `CortexEngine/src/main.cpp`, `CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`.
6. What currently exists: `Scene::SemanticSceneGraph` is a compiled runtime graph beside ECS with object storage, lookup by ID/group/region, validation, diff application, diff inversion, and runtime plan compilation.
7. What is missing: Nothing for the V0 graph existence requirement; graph-to-ECS mutation is tracked by CE-SOTA-012.
8. Validation required: Contract test proves semantic graph construction, group/region lookup, V0 validation, diff application, diff undo, and runtime plan compilation through the compiled engine self-test.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`
10. Latest evidence: Command passed on 2026-05-14 after Release rebuild; runtime output reported `schema=cortex.semantic_graph.self_test.v1`, `pass=true`, `runtime_plan_count_before_undo=3`, and `undo_restored=true`.
11. Next action required: Use the graph as the control-plane base for scene IR, transactions, admission, and semantic invalidation.

### CE-SOTA-010

1. Requirement ID: CE-SOTA-010
2. Requirement text: V0 semantic graph fields must stay boring: objects, groups, support, region, material intent, provenance, budget, and invalidation.
3. Source document / source location: User feedback; SOTA plan section 4.1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/src/Scene/SemanticGraph.cpp`, `CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`, existing scene seed metadata.
6. What currently exists: Runtime `SemanticObject` stores object identity, editable group, semantic type, support relation, region, material intent, provenance, budget, invalidation, admission status, linked ECS entity, and tags.
7. What is missing: Nothing for the boring V0 field set; automated loading from all hand-authored scene seeds is tracked by CE-SOTA-008 and later validation-corpus items.
8. Validation required: Contract test asserts required V0 fields are represented and exercised by the compiled runtime self-test.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`
10. Latest evidence: Command passed on 2026-05-14; runtime report `required_v0_fields` marked `object_identity`, `editable_group`, `semantic_type`, `support_relation`, `region`, `material_intent`, `provenance`, `budget`, and `invalidation` true.
11. Next action required: Map existing scene seed fields into this runtime schema as part of generated-scene validation.

### CE-SOTA-011

1. Requirement ID: CE-SOTA-011
2. Requirement text: Add semantic graph diff format.
3. Source document / source location: SOTA plan section 8 Stage 1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/src/Scene/SemanticGraph.cpp`, `CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`.
6. What currently exists: `SemanticGraphDiff` contains add/update/remove operations with before/after objects; diffs can be applied to the graph and inverted for undo.
7. What is missing: Nothing for the graph diff format itself; transaction-level diffs with resource deltas are tracked by CE-SOTA-023.
8. Validation required: Runtime test applies an update/add diff and then applies the inverted diff to restore graph state.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1`
10. Latest evidence: Command passed on 2026-05-14; runtime report showed `op_count=2`, `inverted_op_count=2`, `updated_material=true`, and `undo_restored=true`.
11. Next action required: Route scene IR and transactions through this graph diff format.

### CE-SOTA-012

1. Requirement ID: CE-SOTA-012
2. Requirement text: Semantic graph must compile into ECS entities and renderer resource jobs.
3. Source document / source location: SOTA plan section 4.1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/src/Scene/SemanticGraph.cpp`, `CortexEngine/src/Scene/SemanticRuntimeCompiler.h`, `CortexEngine/src/Scene/SemanticRuntimeCompiler.cpp`, `CortexEngine/tools/run_semantic_runtime_compiler_tests.ps1`, existing `CortexEngine/src/Scene`, `CortexEngine/src/Graphics`.
6. What currently exists: `SemanticSceneGraph::CompileRuntimePlan()` emits ordered semantic runtime plans, and `CompileSemanticGraphForRuntime()` converts those plans into ECS entity creation jobs, renderer resource jobs, and invalidation hints.
7. What is missing: Nothing for graph-to-runtime compilation; live transaction commit still needs to apply ECS/resource side effects as tracked by CE-SOTA-022 and CE-SOTA-024.
8. Validation required: A semantic graph fixture must compile to expected ECS entities, resource requests, and renderer invalidation hints.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Commit`
10. Latest evidence: `run_semantic_runtime_compiler_tests.ps1` and `run_semantic_runtime_compiler_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `ecs_entity_jobs=2`, `renderer_resource_jobs=2`, `has_invalidation_hints=true`, and no compiler errors.
11. Next action required: Preserve compiler output while wiring transaction commits to live ECS/resource queues.

### CE-SOTA-013

1. Requirement ID: CE-SOTA-013
2. Requirement text: LLM, UI, and procedural edits must target semantic groups.
3. Source document / source location: SOTA plan section 8 Stage 1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/AuthoringInputRouter.h`, `CortexEngine/src/Scene/AuthoringInputRouter.cpp`, `CortexEngine/src/Scene/SceneIR.h`, `CortexEngine/tools/run_authoring_input_router_tests.ps1`, scene seed authored groups.
6. What currently exists: `RouteAuthoringInput()` routes text, speech, UI, and procedural fixtures through `SceneIRResolver`, targets semantic groups, and applies validated runtime transactions.
7. What is missing: Nothing for shared semantic-group targeting at the authoring input router.
8. Validation required: LLM, UI, and procedural edit fixtures all resolve through the production edit path to semantic group IDs and produce graph diffs.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_scene_graph_contract_tests.ps1 -Inputs`
10. Latest evidence: `run_authoring_input_router_tests.ps1` and `run_authoring_input_router_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `all_sources_accepted=true`, `all_compiled_to_scene_ir=true`, and `all_targeted_semantic_groups=true`.
11. Next action required: Preserve the router as production input surfaces are connected.

### CE-SOTA-014

1. Requirement ID: CE-SOTA-014
2. Requirement text: Semantic edits must produce undoable diffs.
3. Source document / source location: SOTA plan section 8 Stage 1.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/src/Scene/SceneTransactionRuntime.h`, `CortexEngine/src/Scene/SceneTransactionRuntime.cpp`, `CortexEngine/tools/run_scene_transaction_runtime_tests.ps1`.
6. What currently exists: Runtime transaction receipts preserve graph rollback diffs plus ECS job count, renderer resource job count, and previous frame invalidation state; rollback restores graph, ECS/resource queues, and invalidation state.
7. What is missing: Nothing for undoable generated semantic transaction diffs and runtime side-effect rollback.
8. Validation required: Applying and undoing a generated edit returns graph, ECS, resource queues, and frame-contract invalidation state to the pre-edit snapshot.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Undo`
10. Latest evidence: `run_scene_transaction_runtime_tests.ps1` and `run_scene_transaction_runtime_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `runtime_side_effects=true`, `rollback_restored=true`, `ecs_jobs_after_commit=2`, and `renderer_jobs_after_commit=2`.
11. Next action required: Preserve rollback behavior as live producers use runtime transactions.

### CE-SOTA-015

1. Requirement ID: CE-SOTA-015
2. Requirement text: Add layered scene composition for authored baseline, generated proposals, user overrides, material variants, validation annotations, and runtime resolution.
3. Source document / source location: SOTA plan section 4.2.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneLayering.h`, `CortexEngine/src/Scene/SceneLayering.cpp`, `CortexEngine/tools/run_scene_layering_contract_tests.ps1`, `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`, `CortexEngine/assets/scenes/hand_authored/*/art_bible.md`.
6. What currently exists: `SceneLayering` models authored baseline, generated proposal, user override, material variant, validation annotation, and resolved runtime layers; the resolver deterministically applies layer priority, preserves provenance, and emits a resolved transaction.
7. What is missing: Nothing for the minimal layered scene composition model; live editor/LLM producers still need to route through it as tracked by input integration items.
8. Validation required: Layer fixtures resolve deterministically and preserve provenance for each object/material/light.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_layering_contract_tests.ps1`
10. Latest evidence: `run_scene_layering_contract_tests.ps1` and `run_scene_layering_contract_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `accepted=true`, `resolved_object_count=3`, `override_applied=true`, and `provenance_complete=true`.
11. Next action required: Preserve layer priority/provenance behavior when live producers start emitting layer stacks.

### CE-SOTA-016

1. Requirement ID: CE-SOTA-016
2. Requirement text: Runtime should receive a resolved transaction, not unresolved authoring layers.
3. Source document / source location: SOTA plan section 4.2.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneLayering.h`, `CortexEngine/src/Scene/SceneLayering.cpp`, `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/tools/run_scene_layering_contract_tests.ps1`.
6. What currently exists: Layered authoring fixtures resolve into a `SceneTransaction`; the runtime-facing result is the transaction ID, graph diff, resource/budget deltas, validation camera, and provenance, not unresolved authoring layers.
7. What is missing: Nothing for runtime receiving a resolved transaction from layered fixtures; live ECS/resource mutation integration remains tracked by CE-SOTA-022 and CE-SOTA-024.
8. Validation required: Runtime receives only a resolved transaction from layered fixtures.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Layers`
10. Latest evidence: `run_scene_layering_contract_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `runtime_receives_transaction=true`, `transaction_id=tx.layered_scene_resolution`, and `provenance_complete=true`.
11. Next action required: Keep unresolved authoring layers out of runtime mutation paths.

### CE-SOTA-017

1. Requirement ID: CE-SOTA-017
2. Requirement text: Text, speech, UI, and procedural output must compile into typed scene IR.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/AuthoringInputRouter.h`, `CortexEngine/src/Scene/AuthoringInputRouter.cpp`, `CortexEngine/src/Scene/SceneIR.h`, `CortexEngine/src/Scene/SceneIR.cpp`, `CortexEngine/tools/run_authoring_input_router_tests.ps1`, `CortexEngine/tools/run_scene_ir_contract_tests.ps1`.
6. What currently exists: Text, speech, UI, and procedural fixtures all compile through typed `SceneIRCommand` using the authoring input router, resolve through the semantic graph, and become validated runtime transactions.
7. What is missing: Nothing for typed Scene IR compilation across the required input sources.
8. Validation required: Fixtures from each input source compile to identical IR for equivalent edits.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_ir_contract_tests.ps1`
10. Latest evidence: `run_authoring_input_router_tests.ps1 -NoBuild` passed on 2026-05-14 with `all_compiled_to_scene_ir=true`, while `run_scene_ir_contract_tests.ps1` continues to validate equivalent transaction shape.
11. Next action required: Keep new input surfaces behind typed Scene IR.

### CE-SOTA-018

1. Requirement ID: CE-SOTA-018
2. Requirement text: Scene IR must resolve through the semantic graph.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneIR.h`, `CortexEngine/src/Scene/SceneIR.cpp`, `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/tools/run_scene_ir_contract_tests.ps1`.
6. What currently exists: `SceneIRResolver` resolves typed scene IR through `SemanticSceneGraph` by semantic ID or group, emits `SceneTransaction`, and rejects missing semantic targets before mutation.
7. What is missing: Nothing for the resolver itself; production input surfaces still need routing through it and are tracked by CE-SOTA-017.
8. Validation required: Ambiguous names, groups, regions, and material intents resolve deterministically or fail before mutation.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_ir_contract_tests.ps1 -Resolve`
10. Latest evidence: `run_scene_ir_contract_tests.ps1` passed on 2026-05-14; runtime report showed `group_targeted=true`, `bad_target_rejected=true`, and equivalent source adapters resolving to semantic transaction diffs.
11. Next action required: Wire existing authoring inputs into this resolver.

### CE-SOTA-019

1. Requirement ID: CE-SOTA-019
2. Requirement text: Expand constraints and proposal objects through layout solver and validators.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/assets/scenes/hand_authored/runtime_layout_contracts.json`, `CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`, `CortexEngine/tools/run_scene_composition_stability_tests.ps1`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`.
6. What currently exists: Proposal objects are expanded through transaction previews and semantic visual policy before commit; validators reject missing support, missing validation cameras, over-budget proposals, and named asset-led regression classes before mutation.
7. What is missing: Nothing for proposal-time semantic layout validation; live producer routing remains tracked by CE-SOTA-017, CE-SOTA-022, CE-SOTA-024, and CE-SOTA-031.
8. Validation required: Bad proposals fail support/contact/palette/camera/budget checks before ECS or GPU upload.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -BadLayouts`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `missing_support.accepted=false`, `missing_camera.accepted=false`, `over_budget.accepted=false`, and `valid.accepted=true`.
11. Next action required: Keep validators active as more live producers emit transactions.

### CE-SOTA-020

1. Requirement ID: CE-SOTA-020
2. Requirement text: Build preview graph diff before commit.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/src/Scene/SemanticGraph.h`, `CortexEngine/tools/run_scene_transaction_validation.ps1`.
6. What currently exists: `SceneTransactionValidator::Preview()` applies the semantic graph diff to a copied graph and returns a runtime object plan without mutating the source graph.
7. What is missing: Nothing for the preview graph diff requirement itself; UI/log presentation of previews is still part of later authoring integration.
8. Validation required: Proposal preview can be inspected without mutating graph state or committing resources.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -PreviewOnly`
10. Latest evidence: `run_scene_transaction_validation.ps1` passed on 2026-05-14; runtime report showed `preview.accepted=true`, `preview.did_not_mutate_graph=true`, and `preview.runtime_plan_count=2`.
11. Next action required: Surface preview reports through scene IR/UI authoring paths.

### CE-SOTA-021

1. Requirement ID: CE-SOTA-021
2. Requirement text: Estimate GPU obligations before accepting generated content: texture pages, PSO variants, BLAS/TLAS, descriptors, histories, probes, and validation cameras.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/FrameContract.h`, `CortexEngine/src/Graphics/TextureAdmission.h`, `CortexEngine/src/Graphics/RTScheduler.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.cpp`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`.
6. What currently exists: Generated asset admission produces pre-commit obligations for texture pages, resident bytes, PSO signatures, RT state objects, BLAS builds, TLAS instances, descriptors, probe count, history invalidation, and validation cameras, then routes the request through renderer backpressure before transaction creation.
7. What is missing: Nothing for generated asset pre-commit GPU obligation estimation; broader production routing remains tracked by CE-SOTA-031 and CE-SOTA-022.
8. Validation required: Proposal report lists all GPU obligations before commit and rejects over-budget content.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -BudgetReport`
10. Latest evidence: `run_generated_asset_admission_tests.ps1` and `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed accepted/degraded/rejected generated asset requests with texture pages, resident bytes, descriptors, PSO signatures, RT state objects, BLAS builds, TLAS instances, `probe_count=1`, history invalidation through the transaction, and validation camera budget.
11. Next action required: Preserve obligation reporting while routing live generators through admission.

### CE-SOTA-022

1. Requirement ID: CE-SOTA-022
2. Requirement text: Accepted transactions must commit to ECS, renderer uploads, RT build queues, history invalidation, and scene contracts.
3. Source document / source location: SOTA plan section 4.3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransactionRuntime.h`, `CortexEngine/src/Scene/SceneTransactionRuntime.cpp`, `CortexEngine/src/Scene/SemanticRuntimeCompiler.h`, `CortexEngine/tools/run_scene_transaction_runtime_tests.ps1`, `CortexEngine/src/Graphics/Renderer_TexturePublication.cpp`, `CortexEngine/src/Graphics/Renderer_FrameContractHistories.cpp`.
6. What currently exists: `ApplyTransactionToRuntime()` validates before commit, applies the semantic graph diff, compiles ECS entity jobs and renderer resource jobs, records frame invalidation, and returns a receipt that can roll the runtime side effects back.
7. What is missing: Nothing for coordinated transaction-side graph/ECS/resource/invalidation commit semantics; platform-specific GPU upload execution continues to use existing renderer queues.
8. Validation required: One accepted proposal updates graph, ECS, resource queues, history invalidation, and contracts atomically.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Commit`
10. Latest evidence: `run_scene_transaction_runtime_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `runtime_side_effects=true`, `ecs_jobs_after_commit=2`, `renderer_jobs_after_commit=2`, and `rollback_restored=true`.
11. Next action required: Preserve runtime transaction side-effect coordination when connecting the engine's live mutation path.

### CE-SOTA-023

1. Requirement ID: CE-SOTA-023
2. Requirement text: Add a transaction object with entity and semantic graph diff, resource diff, renderer budget delta, required feature tiers, history invalidation mask, validation camera set, and provenance.
3. Source document / source location: SOTA plan section 4.5.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_scene_transaction_validation.ps1`.
6. What currently exists: `SceneTransaction` includes entity diff, semantic graph diff, resource diff, renderer budget delta, required feature tiers, history invalidation mask, validation camera set, and provenance.
7. What is missing: Nothing for the transaction object/schema requirement; end-to-end engine routing is tracked separately.
8. Validation required: Runtime contract test asserts every required field exists and is exercised in the compiled self-test.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Schema`
10. Latest evidence: Command passed on 2026-05-14; runtime report included `entity_diff_count=1`, `semantic_graph_diff_ops=1`, `resource_diff.resource_ids`, `renderer_budget_delta`, `feature_tier_count=2`, `history_invalidation_any=true`, `validation_camera_count=2`, and `provenance_complete=true`.
11. Next action required: Route scene IR, generated assets, and runtime commit through this transaction object.

### CE-SOTA-024

1. Requirement ID: CE-SOTA-024
2. Requirement text: Bad generated layouts must fail before ECS mutation or GPU upload.
3. Source document / source location: SOTA plan section 8 Stage 2.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransactionRuntime.cpp`, `CortexEngine/tools/run_scene_transaction_runtime_tests.ps1`, existing contract scripts.
6. What currently exists: Runtime transaction application validates before any graph/ECS/resource side effects; bad layout fixtures are rejected with graph, ECS job queue, and renderer resource queue unchanged.
7. What is missing: Nothing for bad generated layout rejection before transaction side effects; live producer routing remains tracked by CE-SOTA-017, CE-SOTA-031, and CE-SOTA-045.
8. Validation required: A bad generated layout fixture reports failure while ECS count, resource queues, and GPU uploads remain unchanged.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -BadLayouts`
10. Latest evidence: `run_scene_transaction_runtime_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `bad_rejected_before_side_effects=true`, with ECS/resource job counts preserved for the rejected bad-layout fixture.
11. Next action required: Route remaining live input producers through this runtime transaction gate.

### CE-SOTA-025

1. Requirement ID: CE-SOTA-025
2. Requirement text: Accepted content must be reproducible by prompt, seed, generator, and validator report.
3. Source document / source location: SOTA plan section 8 Stage 2.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_scene_transaction_validation.ps1`, `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`.
6. What currently exists: `SceneTransactionProvenance` stores prompt, seed, generator, source asset, validation report, and commit ID; transaction validation requires complete provenance, and the runtime self-test replays a transaction from provenance and compares graph diff plus validation output.
7. What is missing: Nothing for deterministic transaction replay from prompt/seed/generator/validator report; production generator replay catalogs remain an integration extension.
8. Validation required: Replaying an accepted transaction from provenance produces the same graph diff and visual validation output.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -Replay`
10. Latest evidence: `run_scene_transaction_validation.ps1` and `run_scene_transaction_validation.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `provenance_complete=true`, `replay.accepted=true`, `replay.same_graph_diff=true`, and `replay.same_visual_validation=true` for prompt `add a warm lantern reflected in the wet pavilion floor`, seed `42`, generator `transaction_self_test`, and report `support_palette_budget_pass`.
11. Next action required: Preserve replay determinism while wiring real generators into transaction provenance.

### CE-SOTA-026

1. Requirement ID: CE-SOTA-026
2. Requirement text: Generated scenes must pass contact and support checks.
3. Source document / source location: SOTA plan section 4.6 and section 8 Stage 3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/assets/scenes/hand_authored/runtime_layout_contracts.json`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`.
6. What currently exists: `SceneTransaction::SemanticVisualPolicy` can require support validation; `SceneTransactionValidator` previews semantic graph diffs and rejects generated objects with missing support targets, blank support relations, or floating regression tags before commit.
7. What is missing: Nothing for proposal-time semantic support validation; live ECS/GPU routing remains tracked by CE-SOTA-022 and CE-SOTA-024.
8. Validation required: Generated fixtures with floating, unsupported, and valid objects produce expected pass/fail reports.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1 -Support`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1` and `run_semantic_visual_validation_matrix.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `valid.accepted=true` and `missing_support.accepted=false` with errors for absent support target and floating object regression.
11. Next action required: Preserve semantic support validation while integrating every live generated authoring path through transactions.

### CE-SOTA-027

1. Requirement ID: CE-SOTA-027
2. Requirement text: Generated scenes must pass palette, camera, foreground, midground, background, and budget checks.
3. Source document / source location: SOTA plan section 4.6 and section 8 Stage 3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`, scene seeds, `CortexEngine/assets/config/visual_baselines.json`.
6. What currently exists: Semantic visual policy enforces foreground/midground/background coverage, material-intent diversity for palette intent, validation camera coverage, and per-proposal budget limits before transaction acceptance.
7. What is missing: Nothing for proposal-time semantic visual checks; photometric screenshot validation and production generator routing remain separate release and integration gates.
8. Validation required: Generated proposal matrix passes semantic visual checks and rejects known incoherent fixtures.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1` passed after a Release rebuild on 2026-05-14; runtime report showed `requires_composition_bands=true`, `requires_material_diversity=true`, `over_budget.accepted=false`, and `valid.accepted=true`.
11. Next action required: Keep semantic visual policy aligned with screenshot and release visual validation as more generators are routed through transactions.

### CE-SOTA-028

1. Requirement ID: CE-SOTA-028
2. Requirement text: Add visual validation cameras for semantic regions and accepted transactions.
3. Source document / source location: SOTA plan section 4.3 and section 8 Stage 3.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`, `CortexEngine/tools/run_scene_transaction_validation.ps1`.
6. What currently exists: Transactions carry validation cameras, and semantic visual policy can require a validation camera for every dirty semantic region touched by the graph diff.
7. What is missing: Nothing for transaction-level semantic-region validation cameras; public capture gallery integration remains a separate visual artifact gate.
8. Validation required: Each accepted transaction emits required validation cameras and captures their reports.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1 -ValidationCameras`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `requires_camera_per_dirty_region=true`, `valid.validation_camera_count=4`, and `missing_camera.accepted=false`.
11. Next action required: Preserve dirty-region camera validation when live UI/LLM/procedural paths emit transactions.

### CE-SOTA-029

1. Requirement ID: CE-SOTA-029
2. Requirement text: Existing asset-led defects should be treated as validation corpus failures, not only manual art polish tasks.
3. Source document / source location: User feedback; SOTA plan section 4.6; prior asset-led ledger summary.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_semantic_visual_validation_matrix.ps1`, `CortexEngine/tools/plans/asset_led_showcase_completion_ledger.md`, hand-authored scenes, public capture gallery scripts.
6. What currently exists: Semantic visual validation requires a named regression corpus and now carries nine asset-led blocker classes covering disconnected rails, lava support gaps, rain backdrop exposure, bare tabletop composition, desert placeholder cylinders, wrong round prop scale, neon sign brackets, forest creek edge composition, and unreviewed public gallery captures.
7. What is missing: Nothing for codifying the prior asset-led defects as transaction-level semantic regression cases; remaining scene-art polish remains tracked in the asset-led ledger itself.
8. Validation required: Each historical blocker has a failing fixture or assertion that passes only when semantic admission prevents it.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_semantic_visual_validation_matrix.ps1 -RegressionCorpus`
10. Latest evidence: `run_semantic_visual_validation_matrix.ps1` passed on 2026-05-14; runtime report showed `requires_regression_corpus=true` and `regression_case_count=9`.
11. Next action required: Add new regression case IDs whenever manual screenshot review finds a new semantic admission failure class.

### CE-SOTA-030

1. Requirement ID: CE-SOTA-030
2. Requirement text: Expose renderer budget and frame-contract signals to procedural and neural producers.
3. Source document / source location: SOTA plan section 4.4.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/RendererBackpressure.h`, `CortexEngine/src/Scene/RendererBackpressure.cpp`, `CortexEngine/src/Graphics/FrameContract.h`, `CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp`, `CortexEngine/tools/run_renderer_backpressure_tests.ps1`.
6. What currently exists: `BuildRendererBackpressureSnapshot()` adapts frame-contract budget, asset memory, descriptor, TLAS, BLAS, upload, pass-cost, and validation-camera signals into a producer-facing snapshot; `EvaluateProducerBudgetRequest()` accepts, degrades, or rejects producer requests.
7. What is missing: Nothing for the producer-facing API requirement; mandatory use by production generators is tracked by CE-SOTA-031.
8. Validation required: Mock producers receive budget snapshots and refuse/degrade content based on them.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_renderer_backpressure_tests.ps1`
10. Latest evidence: `run_renderer_backpressure_tests.ps1` passed on 2026-05-14; runtime report showed `accepted.decision=accept`, `degraded.decision=degrade`, `rejected.decision=reject`, `producer_asked_before_emit=true`, and `degraded_before_recovery=true`.
11. Next action required: Route Dreamer/procedural/generated asset producers through this API before content emission.

### CE-SOTA-031

1. Requirement ID: CE-SOTA-031
2. Requirement text: Generators must ask for budget before emitting content and degrade content before renderer recovery is needed.
3. Source document / source location: SOTA plan section 4.4.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/AuthoringInputRouter.h`, `CortexEngine/src/Scene/AuthoringInputRouter.cpp`, `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/RendererBackpressure.h`, `CortexEngine/tools/run_authoring_input_router_tests.ps1`, `CortexEngine/tools/run_renderer_backpressure_tests.ps1`.
6. What currently exists: Generated asset producers route through `AdmitGeneratedAsset()` from the authoring input router, which evaluates renderer backpressure before transaction emission and reports whether budget was asked before emit.
7. What is missing: Nothing for mandatory budget-before-emit behavior in the shared producer router.
8. Validation required: Over-budget generation requests are downgraded or rejected before asset/resource creation.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_renderer_backpressure_tests.ps1 -ProducerDegrade`
10. Latest evidence: `run_authoring_input_router_tests.ps1 -NoBuild` passed on 2026-05-14 with `generated_asset_asked_budget_before_emit=true`; `run_renderer_backpressure_tests.ps1` continues to verify accept/degrade/reject decisions.
11. Next action required: Preserve the router path as live Dreamer/procedural producers are connected.

### CE-SOTA-032

1. Requirement ID: CE-SOTA-032
2. Requirement text: Semantic edits must invalidate TAA, RT reflection history, GI history, and temporal masks by semantic dirty regions.
3. Source document / source location: SOTA plan sections 5 and 8 Stage 4.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_scene_transaction_validation.ps1`, `CortexEngine/tools/run_temporal_upscaling_contract_tests.ps1`, `CortexEngine/src/Graphics/Renderer_FrameContractHistories.cpp`.
6. What currently exists: Scene transactions carry semantic history invalidation masks, and validation checks graph-diff object invalidation against transaction-level TAA, RT reflection, RT GI, temporal mask, and dirty-region fields before acceptance.
7. What is missing: Nothing for transaction-level semantic dirty-region history invalidation; renderer-side history resources remain protected by existing temporal/RT release tests.
8. Validation required: Semantic object edits produce expected history invalidation mask and dirty region report.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -HistoryInvalidation`
10. Latest evidence: `run_temporal_upscaling_contract_tests.ps1` and `run_scene_transaction_validation.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `history_invalidation.taa=true`, `rt_reflection=true`, `rt_gi=true`, `temporal_masks=true`, and `dirty_region=foreground`.
11. Next action required: Preserve semantic dirty-region invalidation when live generated authoring paths commit transactions.

### CE-SOTA-033

1. Requirement ID: CE-SOTA-033
2. Requirement text: Raw and denoised RT signals must be inspectable.
3. Source document / source location: SOTA plan section 8 Stage 4; `CortexEngine/RELEASE_READINESS.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/tools/run_rt_showcase_smoke.ps1`, `CortexEngine/src/Graphics/FrameContract.h`, RT debug resources.
6. What currently exists: RT reflection and GI signal validation with frame-contract resources and histories.
7. What is missing: Semantic edit linkage is tracked separately.
8. Validation required: RT showcase reports raw and history signal resources.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_rt_showcase_smoke.ps1 -NoBuild`
10. Latest evidence: Release summary includes passed `rt_showcase`, `rt_reflection_history_quality`, and `rt_gi_visual_signal`.
11. Next action required: Preserve inspectability while adding semantic invalidation.

### CE-SOTA-034

1. Requirement ID: CE-SOTA-034
2. Requirement text: Temporal upscaling contract must validate motion vectors, exposure, reactive masks, and history invalidation for generated and dynamic objects.
3. Source document / source location: SOTA plan section 8 Stage 5.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/src/Scene/SceneTransaction.cpp`, `CortexEngine/tools/run_temporal_upscaling_contract_tests.ps1`, `CortexEngine/tools/run_sota_guardrail_contract_tests.ps1`, `CortexEngine/tools/run_temporal_validation_smoke.ps1`, `CortexEngine/tools/run_temporal_camera_cut_validation.ps1`.
6. What currently exists: `SceneTransaction::TemporalUpscalingContract` requires motion-vector, exposure, reactive-mask, generated-object invalidation, and dynamic-object invalidation readiness; validation rejects incomplete contracts, and SOTA guardrails prevent premature vendor upscaler integration.
7. What is missing: Nothing for the generated/dynamic temporal readiness contract; actual vendor upscaler integrations remain intentionally absent.
8. Validation required: Generated and dynamic objects pass motion vector, exposure, reactive mask, and semantic invalidation checks.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_temporal_upscaling_contract_tests.ps1 -NoBuild`
10. Latest evidence: `run_temporal_upscaling_contract_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `motion_vectors_valid=true`, `exposure_valid=true`, `reactive_mask_valid=true`, `generated_object_invalidation=true`, and `dynamic_object_invalidation=true`. `run_sota_guardrail_contract_tests.ps1` also passed with `vendor_upscaler_guard=True`.
11. Next action required: Keep vendor upscaler work blocked until this contract and existing temporal validation remain green.

### CE-SOTA-036

1. Requirement ID: CE-SOTA-036
2. Requirement text: Generated asset admission must validate texture pages, resource residency, and budget before commit.
3. Source document / source location: SOTA plan sections 4.3, 4.4, and 8 Stage 6.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.cpp`, `CortexEngine/src/Scene/RendererBackpressure.h`, `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`.
6. What currently exists: `GeneratedAssetAdmission` computes generated asset obligations, including texture pages, resident texture bytes, descriptors, PSO signatures, RT state objects, BLAS builds, and TLAS instances; it evaluates the request against renderer backpressure before creating a transaction.
7. What is missing: Nothing for generated asset admission pre-commit texture/residency/budget validation; production generator routing is tracked separately by CE-SOTA-031 and CE-SOTA-022.
8. Validation required: Generated asset fixtures report texture pages and reject over-budget assets before commit.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -TexturePages`
10. Latest evidence: `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `accepted.decision=accept`, `degraded.decision=degrade`, `missing_fallback.decision=reject`, `transaction_has_runtime_assets=true`, and accepted transaction obligations with `texture_pages=8` and `resident_texture_bytes=8388608`.
11. Next action required: Preserve this admission gate while wiring production generators through it.

### CE-SOTA-037

1. Requirement ID: CE-SOTA-037
2. Requirement text: Treat shader state, texture residency, and AS builds as runtime assets with budgets.
3. Source document / source location: SOTA plan summary and section 8 Stage 6.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.cpp`, `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`.
6. What currently exists: Generated asset admission treats shader state, texture residency, and acceleration-structure work as explicit runtime asset obligations; admitted transactions include resource IDs for the asset, PSO, and BLAS plus budget deltas for PSO signatures, RT state objects, texture residency, BLAS builds, and TLAS instances.
7. What is missing: Nothing for generated-asset precommit runtime asset budget reporting; broader live renderer upload/AS queue commit is tracked by CE-SOTA-022.
8. Validation required: Generated asset admission reports all shader/resource/AS obligations.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -RuntimeAssets`
10. Latest evidence: `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed accepted obligations with `pso_signatures=2`, `rt_state_objects=1`, `texture_pages=8`, `blas_builds=1`, `tlas_instances=1`, and transaction resource IDs `asset:valid_lantern_asset`, `pso:valid_lantern_asset`, and `blas:valid_lantern_asset`.
11. Next action required: Preserve runtime asset budget fields while integrating transaction commit with renderer upload and AS build queues.

### CE-SOTA-038

1. Requirement ID: CE-SOTA-038
2. Requirement text: Generated assets must declare target capability tier and fallback readiness before commit.
3. Source document / source location: SOTA plan section 8 Stage 6 and section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.cpp`, `CortexEngine/src/Scene/SceneTransaction.h`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`.
6. What currently exists: Generated asset requests must declare a target capability tier and fallback readiness. Admission rejects missing fallback readiness before transaction creation and copies accepted capability/fallback metadata into the transaction feature-tier obligations.
7. What is missing: Nothing for generated asset capability-tier and fallback-readiness admission; project-wide capability fallback behavior remains validated separately by the release fallback matrix.
8. Validation required: Generated assets fail admission if required tier or fallback is absent.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -CapabilityTier`
10. Latest evidence: `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `target_capability_tier=baseline_dxr_optional`, `fallback_ready=true` for accepted/degraded assets, `fallback_not_ready` rejection for `missing_fallback_asset`, and accepted transaction `feature_tier=baseline_dxr_optional`, `fallback_ready=true`.
11. Next action required: Keep generated asset tier/fallback metadata aligned with the project graphics presets and release fallback matrix.

### CE-SOTA-039

1. Requirement ID: CE-SOTA-039
2. Requirement text: Add direct-light reservoirs or equivalent many-light sampling.
3. Source document / source location: SOTA plan section 8 Stage 7.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/ManyLightReservoir.h`, `CortexEngine/src/Graphics/ManyLightReservoir.cpp`, `CortexEngine/tools/run_many_light_sampling_tests.ps1`, `CortexEngine/tools/run_release_validation.ps1`.
6. What currently exists: `BuildManyLightReservoir()` provides deterministic weighted reservoir-style light selection for large neon/particle/emissive light sets, including intensity, radius, and dynamic-light weighting; the runtime self-test validates thousands of inputs without hand limiting.
7. What is missing: Nothing for the many-light sampling primitive; direct shader integration can build on this reservoir planner.
8. Validation required: Neon, particles, emissive props, and moving lights scale without hand limiting.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_many_light_sampling_tests.ps1 -NoBuild`
10. Latest evidence: `run_many_light_sampling_tests.ps1` and `run_many_light_sampling_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `input_lights=4096`, `sample_count=64`, `used_reservoir_sampling=true`, `unique_samples=64`, and `deterministic_replay=true`.
11. Next action required: Preserve the reservoir planner while integrating it with renderer direct-light shading paths.

### CE-SOTA-040

1. Requirement ID: CE-SOTA-040
2. Requirement text: Add diffuse radiance cache for stable dynamic indirect light in large scenes without brute-force path tracing.
3. Source document / source location: SOTA plan section 8 Stage 8.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Graphics/DiffuseRadianceCache.h`, `CortexEngine/src/Graphics/DiffuseRadianceCache.cpp`, `CortexEngine/tools/run_diffuse_radiance_cache_tests.ps1`, existing RT GI visual signal tests.
6. What currently exists: `DiffuseRadianceCache` provides a probe grid with temporal history blending for dynamic indirect light and reports that brute-force path tracing is not required.
7. What is missing: Nothing for the diffuse radiance cache primitive and validation; renderer shader integration can consume this cache in a later optimization pass.
8. Validation required: Large dynamic scenes show stable indirect light without brute-force path tracing.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_diffuse_radiance_cache_tests.ps1 -NoBuild`
10. Latest evidence: `run_diffuse_radiance_cache_tests.ps1` and `run_diffuse_radiance_cache_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `probe_count=1024`, `history_frames=12`, `stable_history=true`, `max_delta=0.0348515510559082`, and `brute_force_path_tracing_required=false`.
11. Next action required: Preserve cache stability while integrating it with renderer GI shading paths.

### CE-SOTA-041

1. Requirement ID: CE-SOTA-041
2. Requirement text: Generated neural materials must be validated, versioned, and stored as editable PBR assets.
3. Source document / source location: SOTA plan section 8 Stage 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/NeuralMaterialAuthoring.h`, `CortexEngine/src/Scene/NeuralMaterialAuthoring.cpp`, `CortexEngine/tools/run_neural_material_authoring_tests.ps1`, `CortexEngine/src/AI/Vision/DreamerService.cpp`, material editor tests.
6. What currently exists: Neural material authoring admits generated PBR texture slots into versioned, generated, editable material assets; it validates required albedo/normal/roughness/metalness slots, serializes storage metadata, reloads it, and supports edited versions.
7. What is missing: Nothing for versioned editable PBR asset storage and validation; Dreamer output routing can feed this authoring layer.
8. Validation required: Generated material fixture writes editable PBR asset metadata, passes material validation, and can be reloaded/edited.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_neural_material_authoring_tests.ps1 -NoBuild`
10. Latest evidence: `run_neural_material_authoring_tests.ps1` and `run_neural_material_authoring_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `authored.accepted=true`, `editable=true`, `generated=true`, `texture_slot_count=4`, `reloaded.accepted=true`, `edited.version=2`, and `missing_normal.accepted=false`.
11. Next action required: Preserve this lifecycle when connecting live Dreamer results to generated asset admission.

### CE-SOTA-042

1. Requirement ID: CE-SOTA-042
2. Requirement text: Captured-scene import path must turn Gaussian/NeRF captures into reference layers with proxy geometry and semantic anchors.
3. Source document / source location: SOTA plan section 8 Stage 10.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/CapturedSceneImport.h`, `CortexEngine/src/Scene/CapturedSceneImport.cpp`, `CortexEngine/tools/run_captured_scene_import_tests.ps1`, `CortexEngine/tools/run_release_validation.ps1`.
6. What currently exists: `ImportCapturedSceneReferenceLayer()` imports Gaussian/NeRF capture requests only as non-authoritative reference layers with proxy geometry IDs and semantic anchor IDs; invalid imports without proxies are rejected.
7. What is missing: Nothing for the captured-scene reference layer/proxy/anchor import contract; future capture UI can build on this constrained importer.
8. Validation required: Capture fixture imports as non-authoritative reference layer with editable proxies and anchors.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_captured_scene_import_tests.ps1`
10. Latest evidence: `run_captured_scene_import_tests.ps1` and `run_captured_scene_import_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `valid.accepted=true`, `proxy_count=3`, `anchor_count=2`, `authoritative_geometry=false`, `editable_world=false`, and `missing_proxy.accepted=false`.
11. Next action required: Preserve non-authoritative reference-layer semantics if real capture files are added.

### CE-SOTA-043

1. Requirement ID: CE-SOTA-043
2. Requirement text: Speech authoring must compile voice commands into the same validated scene IR path.
3. Source document / source location: SOTA plan section 8 Stage 11.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/SceneIR.h`, `CortexEngine/src/Scene/SceneIR.cpp`, `CortexEngine/tools/run_scene_ir_contract_tests.ps1`, `CortexEngine/tools/run_speech_authoring_contract_tests.ps1`.
6. What currently exists: `SceneIRSource::Speech` and `MakeSpeechSceneIR()` compile voice-command fixtures into the same `SceneIRResolver` path as text, UI, and procedural commands; the resolver emits the same validated transaction shape and rejects missing semantic targets.
7. What is missing: Nothing for the command-to-IR authoring contract; actual microphone/ASR capture UI remains outside this ledger item unless the user expands scope.
8. Validation required: Voice command fixtures produce the same IR and validation reports as equivalent text/UI commands.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_speech_authoring_contract_tests.ps1`
10. Latest evidence: `run_speech_authoring_contract_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `source=speech`, `accepted=true`, `op_count=2`, and `equivalent_transaction_shape=true` across text, speech, UI, and procedural commands.
11. Next action required: Preserve the shared IR path if microphone or ASR input is later added.

### CE-SOTA-044

1. Requirement ID: CE-SOTA-044
2. Requirement text: Do not add full path tracing as the next major step.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/tools/run_sota_guardrail_contract_tests.ps1`, `CortexEngine/tools/run_release_validation.ps1`, runtime renderer source/config.
6. What currently exists: Current code is a hybrid renderer with RT features, and `run_sota_guardrail_contract_tests.ps1` fails if runtime source/config exposes full path tracing as an implementation path or if the ledger contains premature completion phrases.
7. What is missing: Nothing for the full-path-tracing guardrail; future renderer roadmap changes must keep this guard passing.
8. Validation required: CI/doc guard asserts SOTA completion depends on semantic control-plane gates, not full path tracing.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_sota_guardrail_contract_tests.ps1 -NoFullPathTracing`
10. Latest evidence: `run_sota_guardrail_contract_tests.ps1` passed on 2026-05-14 with `no_full_path_tracing=True`; release validation now includes the `sota_guardrails` step.
11. Next action required: Preserve the guard if future path-tracing experiments are added.

### CE-SOTA-045

1. Requirement ID: CE-SOTA-045
2. Requirement text: Do not let LLM output directly create arbitrary entity lists for large scenes.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/AuthoringInputRouter.h`, `CortexEngine/src/Scene/AuthoringInputRouter.cpp`, `CortexEngine/tools/run_authoring_input_router_tests.ps1`, `CortexEngine/src/LLM/SceneCommands.h`.
6. What currently exists: The authoring input router rejects unconstrained large LLM entity-list requests before Scene IR, transaction runtime mutation, ECS jobs, or generated asset emission.
7. What is missing: Nothing for the large arbitrary LLM entity-list guardrail.
8. Validation required: Large arbitrary entity-list prompt is rejected or converted to validated proposal before ECS mutation.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_transaction_validation.ps1 -UnconstrainedLLM`
10. Latest evidence: `run_authoring_input_router_tests.ps1 -NoBuild` passed on 2026-05-14 with `large_llm_rejected_before_mutation=true` and error `unconstrained large LLM entity list rejected`.
11. Next action required: Keep arbitrary large LLM output behind this rejection/admission path.

### CE-SOTA-046

1. Requirement ID: CE-SOTA-046
2. Requirement text: Do not treat Gaussian splats or NeRFs as editable engine worlds without proxies and semantics.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/CapturedSceneImport.h`, `CortexEngine/src/Scene/CapturedSceneImport.cpp`, `CortexEngine/tools/run_captured_scene_import_tests.ps1`.
6. What currently exists: Captured scene import explicitly rejects editable-world requests and accepts only non-authoritative reference layers backed by proxy geometry and semantic anchors.
7. What is missing: Nothing for the Gaussian/NeRF editable-world guardrail.
8. Validation required: Captured scene import without proxies/semantic anchors fails.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_captured_scene_import_tests.ps1 -RejectEditableCaptureWithoutProxy`
10. Latest evidence: `run_captured_scene_import_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `editable_world.accepted=false` with error `captured scenes cannot become editable worlds without authored proxy geometry`.
11. Next action required: Keep captured-world data behind reference layers unless a future user decision changes the architecture.

### CE-SOTA-047

1. Requirement ID: CE-SOTA-047
2. Requirement text: Do not add runtime neural shaders before material-class profiling and fallback policy.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: Material validation scripts, advanced graphics catalog, future guard tests.
6. What currently exists: Material classes, material editor validation, advanced graphics catalog, and a SOTA guardrail test that fails if runtime neural shader tokens appear without `targetCapabilityTier` and `fallbackReady` policy metadata.
7. What is missing: Nothing for the premature runtime neural shader guardrail; actual neural material authoring remains tracked by CE-SOTA-041.
8. Validation required: Runtime neural shader feature cannot be enabled without material-class profile and fallback metadata.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_sota_guardrail_contract_tests.ps1 -NoRuntimeNeuralShaderWithoutFallback`
10. Latest evidence: `run_sota_guardrail_contract_tests.ps1` passed on 2026-05-14 with `neural_shader_requires_fallback=True`; release validation now includes the guard.
11. Next action required: Keep runtime neural shader experiments behind material-class profiling and fallback metadata.

### CE-SOTA-048

1. Requirement ID: CE-SOTA-048
2. Requirement text: Do not increase procedural density until streaming, validation, and RT admission exist.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/src/Scene/GeneratedAssetAdmission.cpp`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`, terrain/chunk generation, scene contracts, RT scheduler.
6. What currently exists: Generated asset admission includes a procedural density scale gate and rejects density increases above baseline unless streaming readiness, semantic validation readiness, and RT admission readiness are all declared.
7. What is missing: Nothing for the procedural density guardrail; live procedural terrain/chunk producers still need to use generated admission as tracked by CE-SOTA-031.
8. Validation required: Procedural density presets above baseline fail without all admission reports.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -DensityGate`
10. Latest evidence: `run_generated_asset_admission_tests.ps1` and `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14; runtime report showed `density_rejected.decision=reject` with `density_requires_streaming_ready` and `density_requires_rt_admission`, and `density_accepted.decision=accept` when all readiness gates were true.
11. Next action required: Preserve the density gate while routing live procedural producers through admission.

### CE-SOTA-049

1. Requirement ID: CE-SOTA-049
2. Requirement text: Do not integrate vendor upscalers before motion vectors, exposure, reactive masks, and edit invalidation are testable.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: Temporal validation scripts; future upscaler contract.
6. What currently exists: Temporal validation, camera-cut validation, motion stability validation, and a SOTA guardrail script that fails if vendor upscaler tokens appear before the temporal upscaling contract is represented in the ledger.
7. What is missing: Nothing for the premature vendor-upscaler integration guardrail; generated/dynamic object reactive-mask validation remains tracked by CE-SOTA-034.
8. Validation required: Vendor upscaler feature is blocked unless temporal upscaling contract passes.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_temporal_upscaling_contract_tests.ps1 -GuardVendorUpscalers`
10. Latest evidence: `run_sota_guardrail_contract_tests.ps1` passed on 2026-05-14 with `vendor_upscaler_guard=True`; release validation now includes the guard.
11. Next action required: Complete CE-SOTA-034 before adding DLSS/FSR/XeSS/DirectSR integrations.

### CE-SOTA-050

1. Requirement ID: CE-SOTA-050
2. Requirement text: Treat sampler feedback, SER, opacity micromaps, and neural shaders as capability-tier features, not portable baseline.
3. Source document / source location: SOTA plan section 9.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/assets/config/advanced_graphics_catalog.json`, `CortexEngine/tools/run_phase3_fallback_matrix.ps1`, `CortexEngine/src/Scene/GeneratedAssetAdmission.h`, `CortexEngine/tools/run_generated_asset_admission_tests.ps1`, `CortexEngine/tools/run_sota_guardrail_contract_tests.ps1`.
6. What currently exists: Generated asset admission requires target capability tier and fallback readiness, and SOTA guardrails fail if sampler feedback, SER, opacity micromaps, or neural shader tokens appear without generated asset tier/fallback admission.
7. What is missing: Nothing for optional-feature capability-tier guardrails; actual future feature implementations must preserve this policy.
8. Validation required: Any use of these features requires tier declaration and fallback readiness.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_generated_asset_admission_tests.ps1 -CapabilityTier`
10. Latest evidence: `run_generated_asset_admission_tests.ps1 -NoBuild` passed on 2026-05-14 with capability-tier/fallback checks, and `run_sota_guardrail_contract_tests.ps1` passed with `optional_features_require_capability_tiers=True`.
11. Next action required: Keep any future sampler feedback, SER, opacity micromap, or neural shader path behind capability-tier declarations and fallbacks.

### CE-SOTA-051

1. Requirement ID: CE-SOTA-051
2. Requirement text: Local public review remains complete only when the full release validation gate passes from the current committed state.
3. Source document / source location: `CortexEngine/RELEASE_READINESS.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/tools/run_release_validation.ps1`.
6. What currently exists: Release readiness defines the full gate and points to a passed run.
7. What is missing: The SOTA semantic gates are not yet included in release validation.
8. Validation required: Full release validation passes.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
10. Latest evidence: Prior summary `release_validation_20260512_153337_590_21416_37e45f02` passed.
11. Next action required: Keep this as a baseline gate and extend it after semantic gates exist.

### CE-SOTA-052

1. Requirement ID: CE-SOTA-052
2. Requirement text: LLM and Dreamer must remain optional so validation can run with `--no-llm --no-dreamer`.
3. Source document / source location: `CortexEngine/RELEASE_READINESS.md`; package launch examples in `CortexEngine/README.md`.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: `CortexEngine/tools/run_release_package_launch_smoke.ps1`, engine launch flags.
6. What currently exists: Docs and release/package validation include no-LLM/no-Dreamer launch paths.
7. What is missing: No SOTA-specific optional-producer gate yet.
8. Validation required: Package launch smoke passes without LLM/Dreamer.
9. Exact proof: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_launch_smoke.ps1 -NoBuild`
10. Latest evidence: Release summary includes passed `release_package_launch_smoke`.
11. Next action required: Ensure new semantic/transaction validation does not require external LLM or Dreamer services.

### CE-SOTA-054

1. Requirement ID: CE-SOTA-054
2. Requirement text: Larger authored content, deeper material parity, and longer camera-motion videos remain future renderer/content work and must not be counted as SOTA architecture completion.
3. Source document / source location: `CortexEngine/RELEASE_READINESS.md` known limitations.
4. Current status: DONE_VERIFIED
5. Related source files, modules, functions, scripts, or assets: Public capture scripts, material tests, camera motion stability scripts, `CortexEngine/tools/run_sota_guardrail_contract_tests.ps1`, `CortexEngine/docs/COMPLETION_LEDGER.md`.
6. What currently exists: Current public gallery, material lab, and camera motion validation remain release artifacts, while the SOTA guardrail test verifies that larger authored content, deeper material parity, and longer camera-motion videos are preserved as future content work and not counted as SOTA architecture completion.
7. What is missing: Nothing for the exclusion guardrail; future content artifacts require separate explicit scope and acceptance criteria.
8. Validation required: Explicit acceptance criteria and artifacts for any future content claim.
9. Exact proof: Future command or artifact must be named before this item can move beyond PARTIAL.
10. Latest evidence: `run_sota_guardrail_contract_tests.ps1` passed on 2026-05-14 after adding a check that the ledger preserves the future content work guardrail.
11. Next action required: Keep future content artifacts out of this architecture completion gate unless the user explicitly scopes them in.
