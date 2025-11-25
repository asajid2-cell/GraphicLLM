# Phase 2: "The Architect" - COMPLETE ✅

## What's New

Phase 2 adds **full llama.cpp integration** for natural language scene control. The engine now features:

- ✅ Real-time LLM inference via llama.cpp
- ✅ Natural language → JSON commands → Scene modifications
- ✅ Thread-safe async command queue
- ✅ SDL3 text input system
- ✅ Mock mode for testing without a model

## Features

### 1. Natural Language Scene Control

Type commands in plain English:
- `"Add a red cube at position 2, 1, 0"`
- `"Make the cube blue and move it up"`
- `"Add a shiny metal sphere next to the cube"`
- `"Remove the red cube"`
- `"Move the camera to 5, 3, 5"`

### 2. The Architect Loop

**Async LLM loop (~1-5 seconds per command)**:
```
User Text → LLM Inference → JSON Output → Command Queue → Scene Update
```

**Main Render Loop (60-120 FPS)** remains unaffected - renders smoothly while LLM thinks.

### 3. Command System

**Supported Commands**:
- `add_entity` - Spawn cube/sphere/plane with position, scale, color
- `remove_entity` - Delete object by name
- `modify_transform` - Change position, rotation, scale
- `modify_material` - Modify color, metallic, roughness, AO
- `modify_camera` - Move camera, change FOV

**Example JSON**:
```json
{
  "commands": [
    {
      "type": "add_entity",
      "entity_type": "sphere",
      "name": "MetalSphere",
      "position": [3, 1, 0],
      "scale": [0.8, 0.8, 0.8],
      "color": [0.7, 0.7, 0.7, 1]
    },
    {
      "type": "modify_material",
      "target": "MetalSphere",
      "metallic": 0.9,
      "roughness": 0.1
    }
  ]
}
```

## How to Use

### Controls

- **T** - Enter text input mode
- **Enter** - Submit command to The Architect
- **ESC** - Cancel text input (or exit app)
- **Backspace** - Delete characters

### Running with Mock LLM (No Model Required)

The engine works out-of-the-box in mock mode with hardcoded responses:

```bash
.\run.ps1
```

**Mock Mode Responses**:
- Any prompt with "red" + "cube" → Adds a red cube
- Any prompt with "sphere" → Adds a green sphere
- Other prompts → Empty command list

### Running with Real LLM

1. **Download Llama 3.2 3B Instruct (Recommended)**:
   ```bash
   cd models
   # Llama 3.2 3B Q4_K_M (~2.0 GB) - Best balance of quality and VRAM usage
   curl -L https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf -o Llama-3.2-3B-Instruct-Q4_K_M.gguf
   ```

   **Why Llama 3.2 3B?**
   - ✅ Reliable JSON generation (better than 1B models)
   - ✅ Strong instruction following
   - ✅ ~2GB VRAM (leaves room for renderer + diffusion model)
   - ✅ 2-4 second inference on CPU
   - ❌ 1B models: Too small for reliable logic
   - ❌ 8B models: Eat all VRAM, no room for graphics

2. **Model path is auto-configured in `main.cpp`**:
   ```cpp
   EngineConfig config;
   config.enableLLM = true;
   config.llmConfig.modelPath = "models/Llama-3.2-3B-Instruct-Q4_K_M.gguf";
   config.llmConfig.contextSize = 4096;  // Llama 3.2 supports larger context
   config.llmConfig.threads = 4;
   config.llmConfig.temperature = 0.7f;
   config.llmConfig.maxTokens = 512;
   ```

3. **Rebuild and run**:
   ```bash
   .\build.ps1
   .\run.ps1
   ```

## Implementation Details

### Architecture

**Core Components**:
- `LLMService` - llama.cpp wrapper, handles inference
- `SceneCommands` - Command types and JSON parser
- `CommandQueue` - Thread-safe queue for async→main thread
- `Prompts` - System prompt + few-shot examples
- `Engine` - Integrates everything, handles text input

**Data Flow**:
```
┌────────────┐
│ User Input │
└──────┬─────┘
       │
       ▼
┌────────────────────┐
│ Engine::SubmitNL() │
└──────┬─────────────┘
       │
       ▼
┌───────────────────┐
│ LLMService        │
│ (Async Thread)    │
│ - Tokenize        │
│ - Inference       │
│ - Generate JSON   │
└──────┬────────────┘
       │
       ▼
┌──────────────────┐
│ CommandParser    │
│ - Parse JSON     │
│ - Create commands│
└──────┬───────────┘
       │
       ▼
┌──────────────────┐
│ CommandQueue     │
│ (Thread-Safe)    │
└──────┬───────────┘
       │
       ▼
┌──────────────────────┐
│ Engine::Update()     │
│ (Main Thread)        │
│ - ExecuteAll()       │
│ - Modify ECS         │
│ - Upload meshes      │
└──────────────────────┘
```

### llama.cpp Integration

**Key Functions Used**:
- `llama_backend_init()` - Initialize backend
- `llama_load_model_from_file()` - Load GGUF model
- `llama_new_context_with_model()` - Create inference context
- `llama_tokenize()` - Convert text to tokens
- `llama_batch_init()` / `llama_batch_add()` - Prepare batches
- `llama_decode()` - Run inference
- `llama_sampler_sample()` - Sample next token
- `llama_token_to_piece()` - Convert token to text

**Performance**:
- Llama 3.2 3B (Q4_K_M): ~2-4 seconds on CPU, reliable JSON output
- 1B models: ~1-2 seconds but unreliable logic/JSON formatting
- 8B models: ~5-10 seconds, high quality but VRAM-heavy
- GPU acceleration possible with CUDA/Metal builds of llama.cpp

### Prompt Engineering

**System Prompt** (see `Prompts.h`):
- Defines The Architect role
- Specifies JSON-only output
- Lists available commands
- Provides coordinate system info
- Includes output format examples

**Few-Shot Examples**:
- 3 examples showing input → JSON output
- Covers common operations
- Guides LLM to consistent format

### Error Handling

- Model loading failures → Falls back to mock mode
- Inference failures → Logs error, continues running
- Invalid JSON → Warns user, skips command
- Missing entities → Logs warning
- Busy LLM → Rejects new requests

## File Structure

```
src/LLM/
├── LLMService.h          - LLM wrapper interface
├── LLMService.cpp        - llama.cpp integration
├── SceneCommands.h       - Command types
├── SceneCommands.cpp     - JSON parser
├── CommandQueue.h        - Thread-safe queue
├── CommandQueue.cpp      - Command execution
└── Prompts.h             - Prompt engineering

vendor/
└── llama.cpp/            - Git submodule

models/
└── (your GGUF models)
```

## Example Session

```
[12:45:00] [info] The Architect is online!
[12:45:00] [info] Press T to enter text input mode
[12:45:05] [info] Text input mode: ON (Press Enter to submit, ESC to cancel)
[12:45:10] [info] Input: Add a blue sphere
[12:45:10] [info] Submitting to Architect: "Add a blue sphere"
[12:45:12] [info] Architect response received (1.87s)
[12:45:12] [info] Parsed 1 commands from JSON
[12:45:12] [info] Queued 1 commands for execution
[12:45:12] [info] Executing: AddEntity: Sphere1 at (0, 1.5, 0)
[12:45:12] [info] Mesh uploaded: 2112 vertices, 3840 indices
[12:45:12] [info] Created entity 'Sphere1' at (0, 1.5, 0)
```

## Performance

**Main Render Loop**: Unaffected, still 60-120+ FPS
**LLM Inference**: 1-5 seconds (async, doesn't block rendering)
**Command Execution**: <1ms (runs on main thread during Update)

## Next Steps (Phase 3: The Dreamer)

- AI texture generation with diffusion models
- Zero-copy CUDA-DX12 interop
- Real-time texture hot-swapping
- Style transfer and prompt-based textures

## Troubleshooting

**"LLM Service initialized (MOCK MODE)"**
- No model path specified in config
- Model file not found
- Solution: Download a GGUF model and set `config.llmConfig.modelPath`

**"Failed to load model from: [path]"**
- Model file doesn't exist
- Incorrect path
- Corrupted model file
- Solution: Re-download model, check path

**Slow inference**
- Large model on CPU
- Solution: Llama 3.2 3B is optimal balance, or build llama.cpp with GPU support (CUDA/ROCm)

**Invalid JSON output**
- Model not following prompt
- Solution: Use Llama 3.2 3B (recommended), adjust temperature, or try Mistral 7B for higher quality

## Credits

- **llama.cpp**: https://github.com/ggerganov/llama.cpp
- **DirectX 12**: Microsoft
- **SDL3**: https://www.libsdl.org/
- **EnTT**: https://github.com/skypjack/entt
