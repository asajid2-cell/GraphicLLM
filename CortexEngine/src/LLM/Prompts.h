#pragma once

#include <string>

namespace Cortex::LLM {

/**
 * Prompt engineering for The Architect
 *
 * System prompts and few-shot examples to guide the LLM
 * to generate valid scene manipulation commands in JSON format
 */
class Prompts {
public:
    // System prompt defining the LLM's role and output format
    static std::string GetSystemPrompt() {
        return R"(You are "The Architect" - an AI assistant controlling a 3D rendering engine.
Return ONLY JSON, no prose, no markdown.
There is an existing object named "SpinningCube". If the user asks to change color/appearance, prefer modify_material on "SpinningCube" over adding new objects.

Allowed commands:
- add_entity: cube|sphere|plane with name, position[3], scale[3], color[4].
- remove_entity: target name.
- modify_transform: target name, position[3], scale[3].
+- modify_material: target name, color[4], metallic, roughness.
- modify_camera: position[3], fov.

Example response:
{"commands":[{"type":"modify_material","target":"SpinningCube","color":[1,0,0,1]}]}

Rules:
- Coordinates: X=right, Y=up, Z=forward (left-handed).
- Colors: RGBA floats 0..1.
- Do not include text outside JSON.)";
    }

    // Few-shot examples to guide the LLM
    static std::string GetFewShotExamples() {
        return R"(
Example 1:
User: "Add a red cube at position 2, 1, 0"
Response:
{"commands":[{"type":"add_entity","entity_type":"cube","name":"Cube1","position":[2,1,0],"scale":[1,1,1],"color":[1,0,0,1]}]}

Example 2:
User: "Make the cube blue and move it up"
Response:
{"commands":[{"type":"modify_material","target":"Cube1","color":[0,0,1,1]},{"type":"modify_transform","target":"Cube1","position":[2,2,0]}]}

Example 3:
User: "Add a shiny metal sphere next to the cube"
Response:
{"commands":[{"type":"add_entity","entity_type":"sphere","name":"Sphere1","position":[3.5,1,0],"scale":[0.8,0.8,0.8],"color":[0.7,0.7,0.7,1]},{"type":"modify_material","target":"Sphere1","metallic":0.9,"roughness":0.1}]}

Example 4:
User: "Turn it red"
Response:
{"commands":[{"type":"modify_material","target":"SpinningCube","color":[1,0,0,1]}]}
)";
    }

    // Build the full prompt for inference
    static std::string BuildPrompt(const std::string& userInput) {
        // Llama 3 chat template to avoid empty replies
        const std::string bos = "<|begin_of_text|>";
        const std::string soh = "<|start_header_id|>";
        const std::string eoh = "<|end_header_id|>";
        const std::string eot = "<|eot_id|>";

        std::string prompt;
        prompt.reserve(1024 + userInput.size());
        prompt += bos;
        prompt += soh; prompt += "system"; prompt += eoh; prompt += "\n";
        prompt += GetSystemPrompt();
        prompt += "\n\nFew-shot:\n";
        prompt += GetFewShotExamples();
        prompt += "\n";
        prompt += eot; prompt += "\n";
        prompt += soh; prompt += "user"; prompt += eoh; prompt += "\n";
        prompt += userInput;
        prompt += "\n";
        prompt += eot; prompt += "\n";
        prompt += soh; prompt += "assistant"; prompt += eoh; prompt += "\n";
        // Do not append eot here; we expect the model to finish the assistant turn.
        return prompt;
    }
};

} // namespace Cortex::LLM
