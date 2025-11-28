# LLM Integration Enhancements

This document records key fixes and improvements made to the LLM integration
layer in Project Cortex.

---

## 1. Token-to-Text Conversion Fix

**File:** `src/LLM/LLMService.cpp`

### Problem

- Tokens returned from the model were frequently converted to empty strings.
- The JSON extractor then saw an empty buffer and failed with:
  “attempting to parse an empty input”.

### Root Cause

- `llama_token_to_piece()` returns a negative value when the provided buffer
  is too small; the absolute value is the required size.
- The original code treated negative values as “error, skip this token”
  instead of “resize and retry”.

### Fix

```cpp
std::vector<char> buf(128);
int wrote = llama_token_to_piece(vocab, token_id, buf.data(),
                                 (int)buf.size(), /* special */ 0);

if (wrote < 0) {
    int needed = -wrote;
    buf.resize(needed + 4); // safety margin
    wrote = llama_token_to_piece(vocab, token_id, buf.data(),
                                 (int)buf.size(), /* special */ 0);
}
```

### Result

- Tokens are now reliably converted to UTF‑8 text.
- The JSON extractor receives the full model output and can locate the
  command block correctly.

---

## 2. Heuristic Fallback for JSON Extraction

**File:** `src/LLM/LLMService.cpp`

### Problem

- In rare cases the model produced extra explanation before or after the JSON.
- The extractor only handled the ideal case: a top‑level object starting at
  the first `{` and ending at the matching `}`.

### Fix

- Implemented a simple brace‑counting scan that:
  - Finds the first `{`.
  - Tracks nesting depth until it returns to zero.
  - Extracts that substring as the candidate JSON.
- If parsing fails, a short diagnostic is logged but the engine continues to run.

### Result

- The system is more tolerant of minor deviations from the expected format.
- Partial or malformed responses no longer crash the command pipeline.

---

## 3. Logging and Regression Tests

**Files:** `src/LLM/RegressionTests.*`

- Added a small set of regression tests that:
  - Simulate common prompts (add lights, modify materials, move camera).
  - Run purely on the command system without involving the model.
  - Log concise before/after summaries of the scene.

This allows validating the command pipeline quickly after code changes,
independent of LLM behavior.

