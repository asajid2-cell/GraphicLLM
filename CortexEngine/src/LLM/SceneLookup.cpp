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
    while (m_recent.size() > 32) {
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

    // Fallback to most recent
    entt::entity fallback = PickMostRecentValid(registry);
    if (isValid(fallback)) {
        outHint = "Falling back to last spawned entity";
        return fallback;
    }

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

    std::map<std::string, int> typeCounts;
    std::ostringstream ss;
    size_t written = 0;

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

        std::ostringstream line;
        line << tag.tag << "(" << type;
        if (!color.empty()) line << "," << color;
        line << ")@";
        line << "(" << std::round(transform.position.x * 10.0f) / 10.0f << ",";
        line << std::round(transform.position.y * 10.0f) / 10.0f << ",";
        line << std::round(transform.position.z * 10.0f) / 10.0f << ")";

        if (written + line.str().size() + 2 < maxChars) {
            if (written == 0) {
                ss << "Entities: ";
            } else {
                ss << "; ";
            }
            ss << line.str();
            written += line.str().size() + 2;
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

    std::string summary = header.str() + ss.str();
    if (summary.size() > maxChars) {
        summary.resize(maxChars);
    }
    return summary;
}

} // namespace Cortex::LLM
