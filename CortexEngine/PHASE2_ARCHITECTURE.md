# Phase 2: "The Architect" - Architecture Document

## Overview

Phase 2 adds **natural language scene control** through an async LLM loop called "The Architect". This allows users to manipulate the 3D scene using plain English commands.

## Architecture

### Three Async Loops

```
┌─────────────────────────────────────────────────────────────┐
│  Main Render Loop (60-120 FPS)                             │
│  - Input processing                                         │
│  - ECS update (rotation, physics)                          │
│  - Execute pending commands from queue                      │
│  - Render scene                                            │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │ Commands
                           │
┌─────────────────────────────────────────────────────────────┐
│  The Architect Loop (~1-5 seconds)                         │
│  - Receive text input                                       │
│  - Build prompt with system instructions + few-shot         │
│  - Run LLM inference (llama.cpp)                           │
│  - Parse JSON response                                      │
│  - Push commands to queue                                   │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

```
User Text Input
    ↓
LLMService.SubmitPrompt()
    ↓
[Async Thread] Llama.cpp Inference
    ↓
JSON Response
    ↓
CommandParser.ParseJSON()
    ↓
CommandQueue.PushBatch()
    ↓
[Main Thread] CommandQueue.ExecuteAll()
    ↓
Scene Modification (ECS)
```

## Components

### 1. LLMService (`src/LLM/LLMService.h/cpp`)

**Purpose**: Wrapper around llama.cpp for async inference

**Key Methods**:
- `Initialize(LLMConfig)` - Load model into memory
- `SubmitPrompt(prompt, callback)` - Async inference
- `Shutdown()` - Cleanup

**Current Status**: ✅ Mock implementation ready
- Returns hard-coded JSON for testing
- Real llama.cpp integration pending

### 2. SceneCommands (`src/LLM/SceneCommands.h/cpp`)

**Purpose**: Define scene manipulation commands

**Command Types**:
- `AddEntityCommand` - Spawn cube/sphere/plane
- `RemoveEntityCommand` - Delete entity by name
- `ModifyTransformCommand` - Move, rotate, scale
- `ModifyMaterialCommand` - Change color, metallic, roughness
- `ModifyCameraCommand` - Move camera, change FOV

**JSON Format**:
```json
{
  "commands": [
    {
      "type": "add_entity",
      "entity_type": "cube",
      "name": "RedCube",
      "position": [2, 1, 0],
      "scale": [1, 1, 1],
      "color": [1, 0, 0, 1]
    }
  ]
}
```

### 3. CommandQueue (`src/LLM/CommandQueue.h/cpp`)

**Purpose**: Thread-safe queue bridging async LLM and main thread

**Key Methods**:
- `Push(command)` - Add command (thread-safe)
- `ExecuteAll(registry, renderer)` - Run all pending (main thread only)

**Execution**:
- Finds entities by tag name
- Modifies ECS components directly
- Uploads new meshes to GPU
- Logs all operations

### 4. Prompts (`src/LLM/Prompts.h`)

**Purpose**: Prompt engineering for consistent LLM output

**Contains**:
- System prompt defining role and JSON format
- Few-shot examples (3 examples)
- Coordinate system explanation
- Output format specification

### 5. Engine Integration (`src/Core/Engine.h/cpp`)

**New Features**:
- `m_llmService` - The Architect instance
- `m_commandQueue` - Command buffer
- `SubmitNaturalLanguageCommand(text)` - Public API
- Text input mode (press T to activate)

## Example Usage

### 1. Add a Red Cube

**User Input**: `"Add a red cube at position 2, 1, 0"`

**LLM Output**:
```json
{
  "commands": [{
    "type": "add_entity",
    "entity_type": "cube",
    "name": "RedCube",
    "position": [2, 1, 0],
    "scale": [1, 1, 1],
    "color": [1, 0, 0, 1]
  }]
}
```

**Result**: Red cube appears in scene

### 2. Change Color and Move

**User Input**: `"Make the cube blue and move it up"`

**LLM Output**:
```json
{
  "commands": [
    {
      "type": "modify_material",
      "target": "RedCube",
      "color": [0, 0, 1, 1]
    },
    {
      "type": "modify_transform",
      "target": "RedCube",
      "position": [2, 2, 0]
    }
  ]
}
```

**Result**: Cube turns blue and moves up

### 3. Add Shiny Sphere

**User Input**: `"Add a shiny metal sphere next to the cube"`

**LLM Output**:
```json
{
  "commands": [
    {
      "type": "add_entity",
      "entity_type": "sphere",
      "name": "Sphere1",
      "position": [3.5, 1, 0],
      "scale": [0.8, 0.8, 0.8],
      "color": [0.7, 0.7, 0.7, 1]
    },
    {
      "type": "modify_material",
      "target": "Sphere1",
      "metallic": 0.9,
      "roughness": 0.1
    }
  ]
}
```

**Result**: Metallic sphere appears

## Implementation Status

### ✅ Completed
- [x] Command system architecture
- [x] JSON command format
- [x] Command parser
- [x] Thread-safe command queue
- [x] Scene command execution
- [x] Prompt engineering system
- [x] Mock LLM service
- [x] Engine integration headers

### 🔄 In Progress
- [ ] Engine.cpp implementation (SubmitNaturalLanguageCommand)
- [ ] Text input handling (SDL3 text events)
- [ ] CMakeLists.txt updates

### 📋 TODO
- [ ] Real llama.cpp integration
- [ ] Model download/setup
- [ ] UI overlay for text input
- [ ] Command history
- [ ] Error handling improvements

## Next Steps

1. **Update CMakeLists.txt** - Add LLM source files
2. **Implement Engine methods** - Wire up text input and LLM calls
3. **Test with mock LLM** - Verify command execution works
4. **Integrate llama.cpp** - Replace mock with real inference
5. **Download model** - Get a small model (e.g., Phi-2, TinyLlama)
6. **Test end-to-end** - Natural language → Scene changes

## Controls (Planned)

- **T** - Enter text input mode
- **Enter** - Submit command to The Architect
- **ESC** - Exit text input mode
- **F1** - Show help overlay

## Performance Targets

- **LLM Inference**: 1-5 seconds (acceptable for creative tool)
- **Command Execution**: <1ms (immediate scene updates)
- **Main Render Loop**: Still 60-120 FPS (unaffected)

## Future Enhancements (Phase 3+)

- Conversational context (remember previous commands)
- Undo/redo system
- Macro recording
- Voice input
- Remote LLM APIs (GPT-4, Claude)
- The Dreamer integration (AI texture generation)
