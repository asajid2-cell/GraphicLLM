# Phase 2 – “The Architect” (Architecture)

Phase 2 introduces an asynchronous LLM loop named **The Architect**.  
Its job is to translate natural-language requests into structured scene commands while
the renderer continues to run at real-time frame rates.

---

## High-Level Design

There are three cooperating loops:

1. **Main Render Loop**
   - Runs at 60–120 FPS.
   - Processes input events.
   - Updates ECS state (animations, scripted behavior).
   - Pulls commands from the LLM command queue and applies them.
   - Renders the scene.

2. **Architect Loop (LLM)**
   - Runs on a background thread.
   - Receives text prompts from the UI.
   - Builds prompts (system instructions + few-shot examples).
   - Calls Llama.cpp to generate constrained JSON.
   - Parses the JSON into typed command structs.
   - Pushes commands into a thread-safe queue.

3. **Dreamer Loop (Diffusion, Phase 3)**
   - Optional in Phase 2; reserved for texture generation.

The render loop never blocks on the LLM; if the LLM takes several seconds,
frames keep rendering with the current scene state.

---

## Data Flow

```text
User text input
        |
        v
UI / TextPrompt -> LLMService::SubmitPrompt()
        |
        v
 [Background Thread]
   - Llama.cpp inference
   - Token-to-text conversion
   - JSON extraction
        |
        v
SceneCommands::ParseJson() -> vector<Command>
        |
        v
CommandQueue::Enqueue()  (thread-safe)
        |
        v
Main thread: CommandQueue::DrainAndApply(ECS_Registry)
```

All communication between threads is done through the `CommandQueue`, which
owns a small FIFO of commands protected by a mutex.

---

## Key Types and Files

- `src/LLM/LLMService.h/.cpp`
  - Owns the Llama.cpp context and worker thread.
  - Provides `SubmitPrompt()` and internal generation loop.

- `src/LLM/SceneCommands.h/.cpp`
  - Defines strongly-typed command structs such as:
    - `AddEntityCommand`
    - `ModifyTransformCommand`
    - `ModifyMaterialCommand`
    - `ModifyCameraCommand`
  - Implements JSON parsing for the LLM’s responses.

- `src/LLM/CommandQueue.h/.cpp`
  - Thread-safe queue holding parsed commands.
  - Exposes `Enqueue()` (LLM thread) and `DrainAndApply()` (render thread).

- `src/LLM/RegressionTests.*`
  - Contains small scripted sequences that exercise the command system
    and verify that the LLM integration remains stable over time.

---

## Prompt and JSON Contract

The LLM is instructed to output **only** a JSON object of the form:

```json
{
  "commands": [
    {
      "type": "add_entity",
      "entity_type": "cube",
      "name": "Example",
      "position": [0, 1, -3],
      "scale": [1, 1, 1],
      "color": [1, 0, 0, 1]
    }
  ]
}
```

The grammar is enforced via a GBNF rule set so that the engine can rely
on well-formed JSON and reject malformed outputs safely.

---

## Threading and Safety

- LLM inference and tokenization happen entirely on the background thread.
- The ECS is only touched on the main thread.
- The `CommandQueue` is the synchronization point; it stores small command
  objects and uses a mutex to guard access.

This design keeps the renderer responsive even when prompts are long or
the model is slow, and makes it straightforward to extend the command set
in later phases.

