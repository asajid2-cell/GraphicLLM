#include "SceneLookup.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

namespace Cortex::LLM {
namespace {
std::string TrimCopy(const std::string& input) {
    auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(begin, end - begin + 1);
}
} // namespace

std::string SceneLookup::Normalize(const std::string& name) {
    std::string trimmed = TrimCopy(name);
    std::string out;
    out.reserve(trimmed.size());
    bool lastWasSpace = false;
    for (char c : trimmed) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) out.push_back(' ');
            lastWasSpace = true;
        } else {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            lastWasSpace = false;
        }
    }
    if (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool SceneLookup::ContainsToken(const std::string& haystack, const std::string& token) {
    auto pos = haystack.find(token);
    if (pos == std::string::npos) return false;
    if (pos > 0 && std::isalnum(static_cast<unsigned char>(haystack[pos - 1]))) return false;
    size_t end = pos + token.size();
    if (end < haystack.size() && std::isalnum(static_cast<unsigned char>(haystack[end]))) return false;
    return true;
}

std::string SceneLookup::ColorLabel(const glm::vec4& color) {
    struct NamedColor { const char* name; glm::vec3 rgb; };
    static const NamedColor palette[] = {
        {"red", {1.0f, 0.0f, 0.0f}}, {"green", {0.0f, 1.0f, 0.0f}}, {"blue", {0.0f, 0.0f, 1.0f}},
        {"yellow", {1.0f, 1.0f, 0.0f}}, {"orange", {1.0f, 0.5f, 0.1f}}, {"purple", {0.5f, 0.2f, 0.8f}},
        {"pink", {1.0f, 0.75f, 0.8f}}, {"teal", {0.0f, 0.5f, 0.5f}}, {"cyan", {0.0f, 1.0f, 1.0f}},
        {"magenta", {1.0f, 0.0f, 1.0f}}, {"white", {1.0f, 1.0f, 1.0f}}, {"black", {0.1f, 0.1f, 0.1f}},
        {"gray", {0.5f, 0.5f, 0.5f}}, {"gold", {1.0f, 0.84f, 0.0f}}, {"silver", {0.75f, 0.75f, 0.75f}},
        {"bronze", {0.8f, 0.5f, 0.2f}}, {"brown", {0.6f, 0.3f, 0.1f}}, {"navy", {0.0f, 0.0f, 0.5f}}
    };

    glm::vec3 rgb = glm::vec3(color);
    float bestDist = std::numeric_limits<float>::max();
    const char* bestName = nullptr;
    for (const auto& candidate : palette) {
        glm::vec3 diff = rgb - candidate.rgb;
        float d2 = glm::dot(diff, diff);
        if (d2 < bestDist) {
            bestDist = d2;
            bestName = candidate.name;
        }
    }
    // Only accept if reasonably close to a named color
    if (bestName && bestDist < 0.25f) {
        return bestName;
    }
    return {};
}

std::string SceneLookup::TypeToString(AddEntityCommand::EntityType type) {
    switch (type) {
        case AddEntityCommand::EntityType::Cube: return "cube";
        case AddEntityCommand::EntityType::Sphere: return "sphere";
        case AddEntityCommand::EntityType::Plane: return "plane";
        case AddEntityCommand::EntityType::Cylinder: return "cylinder";
        case AddEntityCommand::EntityType::Pyramid: return "pyramid";
        case AddEntityCommand::EntityType::Cone: return "cone";
        case AddEntityCommand::EntityType::Torus: return "torus";
        case AddEntityCommand::EntityType::Model: return "model";
    }
    return "object";
}

void SceneLookup::TrackEntity(entt::entity entity,
                              const std::string& tag,
                              AddEntityCommand::EntityType type,
                              const glm::vec4& color) {
    if (entity == entt::null) return;
    Entry e;
    e.id = entity;
    e.displayTag = tag;
    e.normalizedTag = Normalize(tag);
    e.type = type;
    e.colorLabel = ColorLabel(color);
    m_lastSpawned = entity;

    m_recent.push_back(e);
    m_nameToEntity[e.normalizedTag] = entity;
    while (m_recent.size() > 128) {
        auto oldest = m_recent.front();
        m_recent.pop_front();
        // Only erase if this tag still points to that entity
        auto it = m_nameToEntity.find(oldest.normalizedTag);
        if (it != m_nameToEntity.end() && it->second == oldest.id) {
            m_nameToEntity.erase(it);
        }
    }
}

void SceneLookup::ForgetEntity(entt::entity entity) {
    if (entity == entt::null) return;
    for (auto it = m_recent.begin(); it != m_recent.end();) {
        if (it->id == entity) {
            auto mapIt = m_nameToEntity.find(it->normalizedTag);
            if (mapIt != m_nameToEntity.end() && mapIt->second == entity) {
                m_nameToEntity.erase(mapIt);
            }
            it = m_recent.erase(it);
        } else {
            ++it;
        }
    }
    if (m_lastSpawned == entity) {
        m_lastSpawned = entt::null;
    }
}

void SceneLookup::PruneInvalid(Scene::ECS_Registry* registry) const {
    if (!registry) return;
    auto& reg = registry->GetRegistry();
    for (auto it = m_recent.begin(); it != m_recent.end();) {
        if (!reg.valid(it->id)) {
            auto mapIt = m_nameToEntity.find(it->normalizedTag);
            if (mapIt != m_nameToEntity.end() && mapIt->second == it->id) {
                m_nameToEntity.erase(mapIt);
            }
            it = m_recent.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_nameToEntity.begin(); it != m_nameToEntity.end();) {
        if (!reg.valid(it->second)) {
            it = m_nameToEntity.erase(it);
        } else {
            ++it;
        }
    }
    if (m_lastSpawned != entt::null && !reg.valid(m_lastSpawned)) {
        m_lastSpawned = entt::null;
    }
}

void SceneLookup::Rebuild(Scene::ECS_Registry* registry) {
    if (!registry) return;
    m_recent.clear();
    m_nameToEntity.clear();
    m_lastSpawned = entt::null;

    auto view = registry->View<Scene::TagComponent, Scene::RenderableComponent>();
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        const auto& renderable = view.get<Scene::RenderableComponent>(entity);
        // Default unknown type to cube (safe fallback)
        TrackEntity(entity, tag.tag, AddEntityCommand::EntityType::Cube, renderable.albedoColor);
    }
}

entt::entity SceneLookup::PickMostRecentValid(Scene::ECS_Registry* registry) const {
    if (!registry) return entt::null;
    auto& reg = registry->GetRegistry();
    for (auto it = m_recent.rbegin(); it != m_recent.rend(); ++it) {
        if (reg.valid(it->id)) {
            return it->id;
        }
    }
    if (m_lastSpawned != entt::null && reg.valid(m_lastSpawned)) {
        return m_lastSpawned;
    }
    return entt::null;
}

entt::entity SceneLookup::ResolveTarget(const std::string& rawName,
                                        Scene::ECS_Registry* registry,
                                        std::string& outHint) {
    if (!registry) {
        outHint = "No registry available";
        return entt::null;
    }

    PruneInvalid(registry);

    const std::string normalized = Normalize(rawName);
    auto& reg = registry->GetRegistry();

    auto makeNotFoundHint = [&](const std::string& reason) {
        std::ostringstream ss;
        ss << reason;
        if (!m_recent.empty()) {
            ss << " Known: ";
            size_t count = 0;
            for (auto it = m_recent.rbegin(); it != m_recent.rend() && count < 6; ++it, ++count) {
                ss << it->displayTag;
                if (it + 1 != m_recent.rend() && count + 1 < 6) ss << ", ";
            }
        }
        outHint = ss.str();
    };

    auto isValid = [&](entt::entity e) {
        return e != entt::null && reg.valid(e);
    };

    // Pronouns / empty -> last known
    if (normalized.empty() || normalized == "it" || normalized == "that" || normalized == "this" || normalized == "last") {
        entt::entity fallback = PickMostRecentValid(registry);
        if (isValid(fallback)) {
            outHint = "Using last spawned entity";
            return fallback;
        }
        makeNotFoundHint("No recent entity available");
        return entt::null;
    }

    // "last cone", "last cube", "blue one"
    std::string typeToken;
    if (ContainsToken(normalized, "last")) {
        auto pos = normalized.find("last");
        auto rest = Normalize(normalized.substr(pos + 4));
        if (!rest.empty()) typeToken = rest;
    }
    std::string colorToken;
    static const std::vector<std::string> colorWords = {
        "red","green","blue","yellow","orange","purple","pink","teal","cyan","magenta","white","black","gray","grey","gold","silver","bronze","brown","navy"
    };
    for (const auto& c : colorWords) {
        if (ContainsToken(normalized, c)) {
            colorToken = c;
            break;
        }
    }

    auto matchByColorOrType = [&](const std::string& color, const std::string& typeWord) -> entt::entity {
        for (auto it = m_recent.rbegin(); it != m_recent.rend(); ++it) {
            bool matchesColor = color.empty() || it->colorLabel == color;
            bool matchesType = typeWord.empty() || ContainsToken(TypeToString(it->type), typeWord);
            if (matchesColor && matchesType && isValid(it->id)) {
                return it->id;
            }
        }
        return entt::null;
    };

    if (!colorToken.empty() || !typeToken.empty()) {
        entt::entity byAttr = matchByColorOrType(colorToken, typeToken);
        if (isValid(byAttr)) {
            outHint = "Matched recent " + (colorToken.empty() ? "" : colorToken + " ") + (typeToken.empty() ? "object" : typeToken);
            return byAttr;
        }
    }

    // Exact name (case-insensitive)
    auto direct = m_nameToEntity.find(normalized);
    if (direct != m_nameToEntity.end() && isValid(direct->second)) {
        outHint = "Matched exact name";
        return direct->second;
    }

    // Match against current tags (case-insensitive) to catch items created before cache
    auto view = registry->View<Scene::TagComponent>();
    entt::entity substringCandidate = entt::null;
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        std::string tagNorm = Normalize(tag.tag);
        if (tagNorm == normalized) {
            outHint = "Matched exact name";
            return entity;
        }
        if (tagNorm.find(normalized) != std::string::npos || normalized.find(tagNorm) != std::string::npos) {
            substringCandidate = entity;
        }
    }
    if (substringCandidate != entt::null) {
        outHint = "Matched by partial name";
        return substringCandidate;
    }

    // At this point we have no reliable match. To avoid surprising edits to
    // unrelated objects, do NOT silently fall back to the last spawned
    // entity; instead, report a clear "not found" error so the caller can
    // surface this to the user or the LLM.
    makeNotFoundHint("Target '" + rawName + "' not found.");
    return entt::null;
}

std::optional<std::string> SceneLookup::GetLastSpawnedName(Scene::ECS_Registry* registry) const {
    if (!registry || m_recent.empty()) return std::nullopt;
    auto& reg = registry->GetRegistry();
    for (auto it = m_recent.rbegin(); it != m_recent.rend(); ++it) {
        if (reg.valid(it->id)) {
            return it->displayTag;
        }
    }
    return std::nullopt;
}

std::string SceneLookup::BuildSummary(Scene::ECS_Registry* registry, size_t maxChars) const {
    if (!registry) return {};
    PruneInvalid(registry);

    struct GroupStats {
        int count = 0;
        glm::vec3 minPos{0.0f};
        glm::vec3 maxPos{0.0f};
        bool hasBounds = false;
    };

    auto deriveGroupId = [](const std::string& tag) -> std::string {
        // "Bird_A.Body" -> "Bird_A"
        auto dotPos = tag.find('.');
        if (dotPos != std::string::npos && dotPos > 0) {
            return tag.substr(0, dotPos);
        }
        // "Field_Grass_12" -> "Field_Grass"
        auto underscore = tag.find_last_of('_');
        if (underscore != std::string::npos && underscore + 1 < tag.size()) {
            bool allDigits = true;
            for (size_t i = underscore + 1; i < tag.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(tag[i]))) {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits && underscore > 0) {
                return tag.substr(0, underscore);
            }
        }
        return {};
    };

    std::map<std::string, int> typeCounts;
    std::map<std::string, GroupStats> groupStats;
    std::ostringstream perEntity;
    size_t written = 0;

    // Find active camera (if any) to express spatial relations in camera space.
    glm::vec3 camPos(0.0f);
    glm::vec3 camForward(0.0f, 0.0f, 1.0f);
    glm::vec3 camRight(1.0f, 0.0f, 0.0f);
    bool haveCamera = false;
    {
        auto camView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : camView) {
            const auto& cam = camView.get<Scene::CameraComponent>(entity);
            if (!cam.isActive) continue;
            const auto& t = camView.get<Scene::TransformComponent>(entity);
            camPos = t.position;
            camForward = glm::normalize(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            camRight = glm::normalize(glm::cross(camForward, up));
            if (glm::dot(camRight, camRight) < 1e-4f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
                camRight = glm::normalize(glm::cross(camForward, up));
            }
            haveCamera = true;
            break;
        }
    }

    auto view = registry->View<Scene::TagComponent, Scene::RenderableComponent, Scene::TransformComponent>();
    size_t total = 0;
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        const auto& transform = view.get<Scene::TransformComponent>(entity);
        std::string type = "object";
        std::string color;
        for (const auto& entry : m_recent) {
            if (entry.id == entity) {
                type = TypeToString(entry.type);
                color = entry.colorLabel;
                break;
            }
        }
        typeCounts[type]++;
        total++;

        // Grouping by tag prefix to support compounds/patterns like Bird_A.Body, Field_Grass_12, etc.
        std::string groupId = deriveGroupId(tag.tag);
        if (!groupId.empty()) {
            auto& g = groupStats[groupId];
            g.count++;
            if (!g.hasBounds) {
                g.minPos = g.maxPos = transform.position;
                g.hasBounds = true;
            } else {
                g.minPos = glm::min(g.minPos, transform.position);
                g.maxPos = glm::max(g.maxPos, transform.position);
            }
        }

        std::ostringstream line;
        line << tag.tag << "(" << type;
        if (!color.empty()) line << "," << color;
        line << ")";

        // Camera-relative spatial tags to help the LLM reason about layout.
        if (haveCamera) {
            glm::vec3 offset = transform.position - camPos;
            float dist = glm::length(offset);
            float along = glm::dot(offset, camForward);
            float side = glm::dot(offset, camRight);
            float up = offset.y;

            const char* frontBack = (along >= 0.5f) ? "front" : (along <= -0.5f ? "behind" : "mid");
            const char* leftRight = (side >= 0.5f) ? "right" : (side <= -0.5f ? "left" : "center");
            const char* aboveBelow = (up > 0.5f) ? "above" : (up < -0.5f ? "below" : "level");
            const char* nearFar = "mid";
            if (dist < 3.0f) nearFar = "near";
            else if (dist > 12.0f) nearFar = "far";

            line << "[" << frontBack << "," << leftRight << "," << aboveBelow << "," << nearFar << ",d="
                 << std::round(dist * 10.0f) / 10.0f << "]";
        }

        line << "@(" << std::round(transform.position.x * 10.0f) / 10.0f << ",";
        line << std::round(transform.position.y * 10.0f) / 10.0f << ",";
        line << std::round(transform.position.z * 10.0f) / 10.0f << ")";

        const std::string lineStr = line.str();
        if (written + lineStr.size() + 2 < maxChars) {
            if (written == 0) {
                perEntity << "Entities: ";
            } else {
                perEntity << "; ";
            }
            perEntity << lineStr;
            written += lineStr.size() + 2;
        }
    }

    std::ostringstream header;
    header << "Scene: " << total << " objects. Types ";
    size_t added = 0;
    for (const auto& [type, count] : typeCounts) {
        header << type << "=" << count;
        if (++added < typeCounts.size()) header << ", ";
    }
    if (!typeCounts.empty()) header << ". ";

    // Region-style hints for wide groups (e.g., fields, large grids)
    if (!groupStats.empty()) {
        bool first = true;
        for (const auto& [name, g] : groupStats) {
            if (!g.hasBounds) continue;
            glm::vec3 extents = g.maxPos - g.minPos;
            float ex = std::abs(extents.x);
            float ez = std::abs(extents.z);
            if (ex <= 5.0f || ez <= 5.0f) continue;

            float cx = (g.minPos.x + g.maxPos.x) * 0.5f;
            float cz = (g.minPos.z + g.maxPos.z) * 0.5f;

            if (first) {
                header << "Regions ";
                first = false;
            } else {
                header << ", ";
            }
            header << name << ": grid region centered at ("
                   << std::round(cx) << "," << std::round(cz)
                   << ") size~(" << std::round(ex) << "," << std::round(ez) << ")";
        }
        if (!first) {
            header << ". ";
        }
    }

    // Simple motif summary based on group names
    int animals = 0;
    int vehicles = 0;
    int towers = 0;
    for (const auto& [name, g] : groupStats) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.find("cow") != std::string::npos ||
            lower.find("pig") != std::string::npos) {
            animals++;
        }
        if (lower.find("car") != std::string::npos ||
            lower.find("ship") != std::string::npos) {
            vehicles++;
        }
        if (lower.find("tower") != std::string::npos) {
            towers++;
        }
    }
    if (animals > 0 || vehicles > 0 || towers > 0) {
        header << "Motifs: ";
        bool first = true;
        if (animals > 0) {
            header << "FarmAnimals=" << animals;
            first = false;
        }
        if (vehicles > 0) {
            if (!first) header << ", ";
            header << "Vehicles=" << vehicles;
            first = false;
        }
        if (towers > 0) {
            if (!first) header << ", ";
            header << "Towers=" << towers;
        }
        header << ". ";
    }

    // Simple overlap warnings between wide regions (e.g., fields vs roads)
    std::vector<std::pair<std::string, GroupStats>> wideGroups;
    for (const auto& [name, g] : groupStats) {
        if (!g.hasBounds) continue;
        glm::vec3 extents = g.maxPos - g.minPos;
        float ex = std::abs(extents.x);
        float ez = std::abs(extents.z);
        if (ex > 5.0f && ez > 5.0f) {
            wideGroups.emplace_back(name, g);
        }
    }
    int warnings = 0;
    const int kMaxWarnings = 2;
    for (size_t i = 0; i < wideGroups.size() && warnings < kMaxWarnings; ++i) {
        for (size_t j = i + 1; j < wideGroups.size() && warnings < kMaxWarnings; ++j) {
            const auto& a = wideGroups[i].second;
            const auto& b = wideGroups[j].second;

            float overlapMinX = std::max(a.minPos.x, b.minPos.x);
            float overlapMaxX = std::min(a.maxPos.x, b.maxPos.x);
            float overlapMinZ = std::max(a.minPos.z, b.minPos.z);
            float overlapMaxZ = std::min(a.maxPos.z, b.maxPos.z);
            float ox = overlapMaxX - overlapMinX;
            float oz = overlapMaxZ - overlapMinZ;

            // Require a meaningful intersection area to avoid noisy warnings.
            if (ox > 1.0f && oz > 1.0f) {
                header << "Warning: " << wideGroups[i].first
                       << " overlaps " << wideGroups[j].first
                       << "; prefer placing new regions away from each other. ";
                ++warnings;
            }
        }
    }

    // Summarize logical groups (compounds/patterns)
    if (!groupStats.empty()) {
        header << "Groups ";
        size_t printed = 0;
        for (const auto& [name, g] : groupStats) {
            if (g.count < 2) continue; // ignore singletons
            if (printed > 0) header << ", ";
            header << name << "(" << g.count << ")";
            if (++printed >= 4) {
                header << ", ...";
                break;
            }
        }
        if (header.tellp() > 0) header << ". ";
    }

    // Simple spatial structure hints for rows/grids based on group bounds
    std::ostringstream structures;
    size_t structuresWritten = 0;
    for (const auto& [name, g] : groupStats) {
        if (g.count < 3 || !g.hasBounds) continue;
        glm::vec3 extents = g.maxPos - g.minPos;
        float ex = std::abs(extents.x);
        float ez = std::abs(extents.z);
        float ey = std::abs(extents.y);

        std::ostringstream line;
        if (ex > 2.0f * ez && ez < ex * 0.25f) {
            float zMid = (g.minPos.z + g.maxPos.z) * 0.5f;
            line << "Row '" << name << "' of " << g.count << " parts along X near z="
                 << std::round(zMid * 10.0f) / 10.0f;
        } else if (ex > 1.5f && ez > 1.5f) {
            int approxX = static_cast<int>(std::round(std::sqrt(static_cast<float>(g.count))));
            approxX = std::max(1, approxX);
            int approxZ = std::max(1, g.count / approxX);
            line << "Grid '" << name << "' approx " << approxX << "x" << approxZ
                 << " spanning x=[" << std::round(g.minPos.x) << "," << std::round(g.maxPos.x)
                 << "], z=[" << std::round(g.minPos.z) << "," << std::round(g.maxPos.z) << "]";
        } else {
            continue;
        }

        const std::string s = line.str();
        if (structuresWritten + s.size() + 2 < maxChars / 2) {
            if (structuresWritten == 0) {
                structures << " Patterns: ";
            } else {
                structures << "; ";
            }
            structures << s;
            structuresWritten += s.size() + 2;
        }
    }

#if 0
    // Summarize lights separately so the LLM can reason about lighting
    std::ostringstream lights;
    size_t lightsWritten = 0;
    auto lightView = registry->View<Scene::TagComponent, Scene::LightComponent, Scene::TransformComponent>();
    int lightCount = 0;
    for (auto entity : lightView) {
        const auto& tag = lightView.get<Scene::TagComponent>(entity);
        const auto& light = lightView.get<Scene::LightComponent>(entity);
        const auto& transform = lightView.get<Scene::TransformComponent>(entity);

        std::string typeStr = "point";
        switch (light.type) {
            case Scene::LightType::Directional: typeStr = "directional"; break;
            case Scene::LightType::Spot:        typeStr = "spot"; break;
            case Scene::LightType::Point:       typeStr = "point"; break;
        }

        std::ostringstream line;
        line << tag.tag << "(" << typeStr << ",int~"
             << std::round(light.intensity * 10.0f) / 10.0f << ")@("
             << std::round(transform.position.x * 10.0f) / 10.0f << ","
             << std::round(transform.position.y * 10.0f) / 10.0f << ","
             << std::round(transform.position.z * 10.0f) / 10.0f << ")";

        const std::string s = line.str();
        if (lightsWritten + s.size() + 2 < maxChars / 4) {
            if (lightsWritten == 0) {
                lights << " Lights: ";
            } else {
                lights << "; ";
            }
            lights << s;
            lightsWritten += s.size() + 2;
        }
        ++lightCount;
        if (lightCount >= 4) {
            break;
        }
    }
#endif

    // Build a cleaned light summary used in the final text
    std::ostringstream lights2;
    {
        auto viewLights = registry->View<Scene::TagComponent, Scene::LightComponent, Scene::TransformComponent>();
        size_t written = 0;
        size_t count = 0;
        for (auto entity : viewLights) {
            const auto& tag2 = viewLights.get<Scene::TagComponent>(entity);
            const auto& light2 = viewLights.get<Scene::LightComponent>(entity);
            const auto& transform2 = viewLights.get<Scene::TransformComponent>(entity);

            std::string typeStr2 = "point";
            switch (light2.type) {
                case Scene::LightType::Directional: typeStr2 = "directional"; break;
                case Scene::LightType::Spot:        typeStr2 = "spot"; break;
                case Scene::LightType::Point:       typeStr2 = "point"; break;
            }

            std::ostringstream line2;
            line2 << tag2.tag << "(" << typeStr2 << ",I="
                  << std::round(light2.intensity * 10.0f) / 10.0f << ")@("
                  << std::round(transform2.position.x * 10.0f) / 10.0f << ","
                  << std::round(transform2.position.y * 10.0f) / 10.0f << ","
                  << std::round(transform2.position.z * 10.0f) / 10.0f << ")";

            const std::string s2 = line2.str();
            if (written + s2.size() + 2 < maxChars / 4) {
                if (written == 0) {
                    lights2 << " Lights: ";
                } else {
                    lights2 << "; ";
                }
                lights2 << s2;
                written += s2.size() + 2;
            }

            if (++count >= 4) {
                break;
            }
        }
    }

    std::string summary = header.str() + lights2.str() + perEntity.str() + structures.str();
    if (summary.size() > maxChars) {
        summary.resize(maxChars);
    }
    return summary;
}

} // namespace Cortex::LLM
