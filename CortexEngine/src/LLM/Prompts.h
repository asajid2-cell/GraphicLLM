#pragma once

#include <string>
#include <sstream>

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
    static std::string GetSystemPrompt(bool hasShowcase) {
        const std::string preferredTarget = hasShowcase ? "SpinningCube" : "RecentObject";

        std::ostringstream ss;
        ss << "You are \"The Architect\" - an AI assistant controlling a 3D rendering engine.\n";
        ss << "Return ONLY JSON, no prose, no markdown.\n";
        if (hasShowcase) {
            ss << "There is currently an object named \"SpinningCube\" at origin. If the user asks to change color/appearance, prefer modify_material on \"SpinningCube\" over adding new objects.\n";
        } else {
            ss << "If the scene summary lists an existing \"SpinningCube\", prefer modify_material on it over adding new objects. Otherwise, use the most recent object when the user says \"it\".\n";
        }
        ss << "You may receive a scene summary; use it to target existing entities by name instead of inventing new ones.\n\n";
        ss << "Allowed commands:\n"
           << "- add_entity: cube|sphere|plane|cylinder|pyramid|cone|torus with name, position[3], scale[3], color[4], metallic (0-1), roughness (0-1), ao (0-1).\n"
           << "- remove_entity: target name.\n"
           << "- modify_transform: target name, position[3], rotation[3] (Euler angles in degrees), scale[3].\n"
           << "- modify_material: target name, color[4], metallic (0-1), roughness (0-1).\n"
           << "- modify_camera: position[3], fov.\n\n";
        ss << "Positioning guidelines:\n"
           << "- Origin (0,0,0) may already have an object; consult the scene summary before placing on top of it\n"
           << "- Place new objects offset: left (-2 to -4, Y, Z), right (2 to 4, Y, Z), or forward/back (X, Y, -3 to 3)\n"
           << "- Keep Y > 0 (above ground plane at Y=-0.5)\n"
           << "- Default scale: 1.0 for most objects\n\n";
        ss << "Material guidelines:\n"
           << "- Shiny/metallic: metallic=1, roughness=0-0.2 (mirror-like reflections)\n"
           << "- Matte/dull: metallic=0, roughness=0.8-1.0 (no reflections, diffuse)\n"
           << "- Soft/smooth: metallic=0, roughness=0.3-0.5 (slight sheen)\n"
           << "- Rough/textured: metallic=0, roughness=0.7-1.0 (coarse surface)\n"
           << "- Mirror-like: metallic=1, roughness=0 (perfect reflection)\n"
           << "- Glossy metal: metallic=1, roughness=0.1-0.3 (polished metal)\n\n";
        ss << "Supported shapes: cube, sphere, plane, cylinder, pyramid, cone, torus (NOT triangle/square - use cube/plane instead)\n\n";
        ss << "Supported colors (RGBA 0-1): red, blue, green, yellow, cyan, magenta, orange, purple, pink,\n"
           << "lime, teal, violet, brown, tan, maroon, olive, navy, turquoise, gold, silver, bronze,\n"
           << "white, black, gray, lightgray, darkgray.\n\n";
        ss << "Example response:\n";
        ss << "{\"commands\":[{\"type\":\"modify_material\",\"target\":\"" << preferredTarget << "\",\"color\":[1,0,0,1]}]}\n\n";
        ss << "Rules:\n"
           << "- Coordinates: X=right, Y=up, Z=forward (left-handed).\n"
           << "- Colors: RGBA floats 0..1.\n"
           << "- Do not include text outside JSON.";
        return ss.str();
    }

    // Few-shot examples to guide the LLM
    static std::string GetFewShotExamples(bool hasShowcase) {
        const std::string target = hasShowcase ? "SpinningCube" : "RecentObject";
        std::ostringstream ss;
        auto addExample = [&](const std::string& header, const std::string& response) {
            ss << header << "\nResponse:\n" << response << "\n\n";
        };

        addExample("Example 1:\nUser: \"Make it blue\"",
                   "{\"commands\":[{\"type\":\"modify_material\",\"target\":\"" + target + "\",\"color\":[0,0,1,1]}]}");

        addExample("Example 2:\nUser: \"Make it bigger\"",
                   "{\"commands\":[{\"type\":\"modify_transform\",\"target\":\"" + target + "\",\"scale\":[2,2,2]}]}");

        addExample("Example 3:\nUser: \"Make it red and move it up\"",
                   "{\"commands\":[{\"type\":\"modify_material\",\"target\":\"" + target + "\",\"color\":[1,0,0,1]},{\"type\":\"modify_transform\",\"target\":\"" + target + "\",\"position\":[0,2,0]}]}");

        addExample("Example 4:\nUser: \"Add a gold sphere on the left\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"sphere\",\"name\":\"GoldSphere\",\"position\":[-3,1,0],\"scale\":[1,1,1],\"color\":[1,0.84,0,1]}]}");

        addExample("Example 5:\nUser: \"Add three spheres: red, green, and blue\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"sphere\",\"name\":\"RedSphere\",\"position\":[-3,1,0],\"scale\":[0.8,0.8,0.8],\"color\":[1,0,0,1]},{\"type\":\"add_entity\",\"entity_type\":\"sphere\",\"name\":\"GreenSphere\",\"position\":[0,1,2],\"scale\":[0.8,0.8,0.8],\"color\":[0,1,0,1]},{\"type\":\"add_entity\",\"entity_type\":\"sphere\",\"name\":\"BlueSphere\",\"position\":[3,1,0],\"scale\":[0.8,0.8,0.8],\"color\":[0,0,1,1]}]}");

        addExample("Example 6:\nUser: \"Add a turquoise cylinder\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"cylinder\",\"name\":\"TurquoiseCylinder\",\"position\":[0,1,-3],\"scale\":[1,1,1],\"color\":[0.25,0.88,0.82,1]}]}");

        addExample("Example 7:\nUser: \"Create a gold pyramid on the right\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"pyramid\",\"name\":\"GoldPyramid\",\"position\":[3,0.5,0],\"scale\":[1,1,1],\"color\":[1,0.84,0,1]}]}");

        addExample("Example 8:\nUser: \"Rotate it 45 degrees\"",
                   "{\"commands\":[{\"type\":\"modify_transform\",\"target\":\"" + target + "\",\"rotation\":[0,45,0]}]}");

        addExample("Example 9:\nUser: \"Make it shiny\"",
                   "{\"commands\":[{\"type\":\"modify_material\",\"target\":\"" + target + "\",\"metallic\":1,\"roughness\":0.1}]}");

        addExample("Example 10:\nUser: \"Add a matte red sphere\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"sphere\",\"name\":\"MatteRedSphere\",\"position\":[-3,1,0],\"scale\":[1,1,1],\"color\":[1,0,0,1],\"metallic\":0,\"roughness\":0.9}]}");

        addExample("Example 11:\nUser: \"Create a shiny silver cone\"",
                   "{\"commands\":[{\"type\":\"add_entity\",\"entity_type\":\"cone\",\"name\":\"ShinySilverCone\",\"position\":[3,1,0],\"scale\":[1,1,1],\"color\":[0.75,0.75,0.75,1],\"metallic\":1,\"roughness\":0.05}]}");

        return ss.str();
    }

    // Build the full prompt for inference
    static std::string BuildPrompt(const std::string& userInput, const std::string& sceneSummary, bool hasShowcase) {
        // Llama 3 chat template to avoid empty replies
        const std::string bos = "<|begin_of_text|>";
        const std::string soh = "<|start_header_id|>";
        const std::string eoh = "<|end_header_id|>";
        const std::string eot = "<|eot_id|>";

        std::string prompt;
        prompt.reserve(1024 + userInput.size());
        prompt += bos;
        prompt += soh; prompt += "system"; prompt += eoh; prompt += "\n";
        prompt += GetSystemPrompt(hasShowcase);
        prompt += "\n\nFew-shot:\n";
        prompt += GetFewShotExamples(hasShowcase);
        if (!sceneSummary.empty()) {
            prompt += "\nScene state:\n";
            prompt += sceneSummary;
            prompt += "\nEnd scene state.";
        }
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
