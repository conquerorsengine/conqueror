#include "GameplayTags.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>

namespace Conqueror
{
    namespace
    {
        GameplayTags::ExtraTagSource s_Extra = nullptr;

        std::vector<std::string> ParseList(const std::string& text)
        {
            std::vector<std::string> out;
            std::string cur;
            auto flush = [&]() {
                std::string t = GameplayTags::Normalize(cur);
                if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end())
                    out.push_back(t);
                cur.clear();
            };
            for (char c : text)
            {
                if (c == ',' || c == ';' || c == '\n' || c == '\r')
                    flush();
                else
                    cur += c;
            }
            flush();
            return out;
        }

        std::string Join(const std::vector<std::string>& tags)
        {
            std::string out;
            for (const auto& t : tags)
                out += (out.empty() ? "" : ", ") + t;
            return out;
        }

        void Refresh(GameplayTagsComponent& c)
        {
            if (c.ParsedFrom == c.Tags && (!c.Parsed.empty() || c.Tags.empty()))
                return;
            c.Parsed = ParseList(c.Tags);
            c.ParsedFrom = c.Tags;
        }
    }

    namespace GameplayTags
    {
        std::string Normalize(const std::string& tag)
        {
            std::string out;
            for (char c : tag)
            {
                if (std::isspace((unsigned char)c))
                    continue;
                if (c == '.' && (out.empty() || out.back() == '.'))
                    continue;
                out += c;
            }
            while (!out.empty() && out.back() == '.')
                out.pop_back();
            return out;
        }

        bool IsValidTag(const std::string& tag)
        {
            if (tag.empty())
                return false;
            return std::all_of(tag.begin(), tag.end(), [](char c) { return std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'; });
        }

        bool Matches(const std::string& owned, const std::string& query, bool exact)
        {
            if (query.empty())
                return false;
            if (owned.size() == query.size())
            {
                // Büyük/küçük harf duyarsız eşitlik
                return std::equal(owned.begin(), owned.end(), query.begin(), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            }
            if (exact || owned.size() < query.size() + 1 || owned[query.size()] != '.')
                return false;
            return std::equal(query.begin(), query.end(), owned.begin(), [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
        }

        const std::vector<std::string>& Get(Entity entity)
        {
            static const std::vector<std::string> empty;
            if (!entity || !entity.HasComponent<GameplayTagsComponent>())
                return empty;
            auto& c = entity.GetComponent<GameplayTagsComponent>();
            Refresh(c);
            return c.Parsed;
        }

        std::vector<std::string> GetAll(Entity entity)
        {
            std::vector<std::string> all = Get(entity);
            if (s_Extra && entity)
                for (auto& t : s_Extra(entity))
                    if (std::find(all.begin(), all.end(), t) == all.end())
                        all.push_back(t);
            return all;
        }

        bool Has(Entity entity, const std::string& query, bool exact)
        {
            const std::string q = Normalize(query);
            for (const auto& t : Get(entity))
                if (Matches(t, q, exact)) return true;
            if (s_Extra && entity)
                for (const auto& t : s_Extra(entity))
                    if (Matches(t, q, exact)) return true;
            return false;
        }

        bool HasAny(Entity entity, const std::vector<std::string>& queries, bool exact)
        {
            return std::any_of(queries.begin(), queries.end(), [&](const std::string& q) { return Has(entity, q, exact); });
        }

        bool HasAll(Entity entity, const std::vector<std::string>& queries, bool exact)
        {
            return std::all_of(queries.begin(), queries.end(), [&](const std::string& q) { return Has(entity, q, exact); });
        }

        bool Add(Entity entity, const std::string& tag)
        {
            const std::string t = Normalize(tag);
            if (!entity || !IsValidTag(t))
                return false;
            auto& c = entity.HasComponent<GameplayTagsComponent>() ? entity.GetComponent<GameplayTagsComponent>() : entity.AddComponent<GameplayTagsComponent>();
            Refresh(c);
            for (const auto& owned : c.Parsed)
                if (Matches(owned, t, true)) return false;
            c.Parsed.push_back(t);
            c.Tags = Join(c.Parsed);
            c.ParsedFrom = c.Tags;
            return true;
        }

        bool Remove(Entity entity, const std::string& tag, bool removeChildren)
        {
            if (!entity || !entity.HasComponent<GameplayTagsComponent>())
                return false;
            auto& c = entity.GetComponent<GameplayTagsComponent>();
            Refresh(c);
            const std::string t = Normalize(tag);
            const size_t before = c.Parsed.size();
            c.Parsed.erase(std::remove_if(c.Parsed.begin(), c.Parsed.end(), [&](const std::string& owned) {
                return removeChildren ? Matches(owned, t, false) : Matches(owned, t, true);
            }), c.Parsed.end());
            if (c.Parsed.size() == before)
                return false;
            c.Tags = Join(c.Parsed);
            c.ParsedFrom = c.Tags;
            return true;
        }

        std::vector<uint32_t> FindEntities(Scene& scene, const std::string& query, bool exact)
        {
            std::vector<uint32_t> out;
            auto view = scene.m_Registry.view<TagComponent>();
            for (auto h : view)
            {
                if (!scene.IsEntityActive(h))
                    continue;
                Entity e{ h, &scene };
                if (Has(e, query, exact))
                    out.push_back((uint32_t)h);
            }
            return out;
        }

        void SetExtraSource(ExtraTagSource source) { s_Extra = source; }
    }

    void RegisterGameplayTagComponents()
    {
        using G = GameplayTagsComponent;
        ComponentDef<G>("Gameplay Tags", "GameplayTagsComponent", "Gameplay", ICON_FA_TAGS)
            .Describe("Hierarchical tags (Unreal Gameplay Tags): \"Status.Burning, Team.Red\". Asking for \"Status\" also matches \"Status.Burning\". Scripts: TagAdd, TagHas, TagRemove, FindEntitiesWithGameplayTag.")
            .Field("Tags", "Tags", &G::Tags).Tooltip("Comma separated, dot hierarchy: Status.Burning, Team.Red");
    }
}
