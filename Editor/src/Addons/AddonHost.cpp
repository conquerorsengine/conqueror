#include "AddonHost.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scripting/ConquerorScript/CQSVM.h"
#include "Scripting/ConquerorScript/CQSLexer.h"
#include "Scripting/ConquerorScript/CQSParser.h"
#include "Scripting/ConquerorScript/CQSAST.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "UI/EditorUI.h"
#include "UI/IconNames.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Conqueror::Editor
{
    using CQS::Value;

    namespace
    {
        uint32_t EntityHook(uint32_t entity)
        {
            return ScriptContext::SetCurrentEntity(entity);
        }

        void CollectClasses(const CQS::ASTNodePtr& node, std::vector<std::string>& out)
        {
            using namespace CQS;
            if (!node) return;
            if (node->Type == NodeType::Program)
                for (auto& s : static_cast<ProgramNode*>(node.get())->Statements) CollectClasses(s, out);
            else if (node->Type == NodeType::NamespaceStmt)
                for (auto& s : static_cast<NamespaceNode*>(node.get())->Body) CollectClasses(s, out);
            else if (node->Type == NodeType::ClassDecl || node->Type == NodeType::ScriptDecl)
                out.push_back(static_cast<ClassDeclNode*>(node.get())->Name);
        }

        std::string Arg(int argc, Value* args, int i, const std::string& def = "")
        {
            return i < argc && !args[i].IsNull() ? args[i].ToString() : def;
        }
        double Num(int argc, Value* args, int i, double def = 0.0)
        {
            return i < argc && args[i].IsNumber() ? args[i].ToNumber() : def;
        }

        Value Vec3Value(const glm::vec3& v)
        {
            auto* m = new CQS::CQSMapObject();
            m->Entries["x"] = Value::MakeFloat(v.x);
            m->Entries["y"] = Value::MakeFloat(v.y);
            m->Entries["z"] = Value::MakeFloat(v.z);
            return Value::MakeObject(m);
        }

        // Addon çağrıları sırasında betik bağlamı: editör sahnesi, varlık yok
        struct EditorScriptScope
        {
            Scene* PrevScene;
            uint32_t PrevEntity;
            explicit EditorScriptScope(Scene* scene)
                : PrevScene(ScriptContext::GetActiveScene()), PrevEntity(ScriptContext::SetCurrentEntity(ScriptContext::NullEntity))
            {
                ScriptContext::SetActiveScene(scene);
            }
            ~EditorScriptScope()
            {
                ScriptContext::SetActiveScene(PrevScene);
                ScriptContext::SetCurrentEntity(PrevEntity);
            }
        };
    }

    AddonHost& AddonHost::Get()
    {
        static AddonHost s_Host;
        return s_Host;
    }

    AddonHost::Instance* AddonHost::FindInstance(const std::string& id)
    {
        for (auto& inst : m_Instances)
            if (inst->Manifest.Id == id)
                return inst.get();
        return nullptr;
    }

    void AddonHost::Notify(Instance& inst, const std::string& message)
    {
        CQ_CORE_INFO("[Addon:{0}] {1}", inst.Manifest.Name, message);
        inst.Messages.push_back(message);
        if (inst.Messages.size() > 32)
            inst.Messages.erase(inst.Messages.begin());
    }

    // ------------------------------------------------------------------ Yükleme

    void AddonHost::Sync()
    {
        const std::string packages = AddonManager::GetPackagesDirectory().string();
        if (m_LoadedVersion == AddonManager::GetVersion() && m_LoadedPackages == packages)
            return;
        ReloadAll();
    }

    void AddonHost::UnloadAll()
    {
        for (auto& inst : m_Instances)
        {
            if (inst->Loaded && inst->VM)
            {
                Call(*inst, "OnUnload");
                inst->VM->Unpin(inst->PinHandle);
            }
        }
        m_Instances.clear();
    }

    void AddonHost::ReloadAll()
    {
        // Panel açık/kapalı durumunu yeniden yüklemede koru
        std::vector<std::pair<std::string, std::string>> openPanels;
        for (auto& inst : m_Instances)
            for (auto& p : inst->Panels)
                if (p.Open)
                    openPanels.push_back({ inst->Manifest.Id, p.Title });

        UnloadAll();
        AddonManager::Refresh();
        for (const auto& manifest : AddonManager::GetInstalled())
        {
            if (!AddonManager::IsEnabled(manifest.Id))
                continue;
            auto inst = std::make_unique<Instance>();
            inst->Manifest = manifest;
            if (!manifest.EditorScript.empty())
                Load(*inst);
            m_Instances.push_back(std::move(inst));
        }
        for (auto& [id, title] : openPanels)
            OpenPanel(id, title);
        m_LoadedVersion = AddonManager::GetVersion();
        m_LoadedPackages = AddonManager::GetPackagesDirectory().string();
    }

    bool AddonHost::Load(Instance& inst)
    {
        namespace fs = std::filesystem;
        fs::path scriptPath = inst.Manifest.Directory / inst.Manifest.EditorScript;
        std::ifstream file(scriptPath, std::ios::binary);
        if (!file.is_open())
        {
            inst.Error = "Editor script not found: " + scriptPath.string();
            CQ_CORE_ERROR("[Addon:{0}] {1}", inst.Manifest.Name, inst.Error);
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();

        inst.VM = std::make_unique<CQS::CQSVM>();
        Instance* self = &inst;
        inst.VM->SetErrorCallback([this, self](const std::string& message) {
            self->Error = message;
            CQ_CORE_ERROR("[Addon:{0}] {1}", self->Manifest.Name, message);
        });
        inst.VM->SetEntityContextHook(&EntityHook);
        Scripting::NativeRegistry::Get().BindTo(inst.VM.get());
        RegisterNatives(inst);

        std::vector<std::string> errors;
        CQS::CQSFunctionObject* module = inst.VM->Compile(source, scriptPath.string(), errors);
        if (!module)
        {
            inst.Error.clear();
            for (auto& e : errors)
                inst.Error += (inst.Error.empty() ? "" : "\n") + e;
            CQ_CORE_ERROR("[Addon:{0}] Compile failed:\n{1}", inst.Manifest.Name, inst.Error);
            return false;
        }
        std::shared_ptr<Scene> scene = m_Hooks.GetScene ? m_Hooks.GetScene() : nullptr;
        EditorScriptScope scope(scene.get());
        if (!inst.VM->RunModule(module))
        {
            inst.Error = inst.VM->GetLastError();
            return false;
        }

        // İlk script/class bildirimi addon nesnesidir
        CQS::CQSLexer lexer;
        CQS::CQSParser parser;
        std::vector<std::string> classes;
        CollectClasses(parser.Parse(lexer.Tokenize(source)), classes);
        CQS::CQSClassObject* cls = nullptr;
        for (auto& name : classes)
            if ((cls = inst.VM->FindClass(name)))
                break;
        if (!cls)
        {
            inst.Error = "The editor script must declare a script class (e.g. 'script MyAddon : ConquerorScript { ... }').";
            return false;
        }
        CQS::CQSInstanceObject* object = inst.VM->Instantiate(cls, nullptr, 0, ScriptContext::NullEntity);
        if (!object)
        {
            inst.Error = inst.VM->GetLastError();
            return false;
        }
        inst.Object = Value::MakeObject(object);
        inst.PinHandle = inst.VM->Pin(inst.Object);
        inst.Loaded = true;
        inst.Error.clear();
        inst.HasUpdate = object->Class && object->Class->FindMethod("OnEditorUpdate");
        Call(inst, "OnLoad");
        CQ_CORE_INFO("[Addons] Loaded {0} {1} ({2} menu items, {3} buttons, {4} panels)", inst.Manifest.Name, inst.Manifest.Version,
                     inst.Menu.size(), inst.Buttons.size(), inst.Panels.size());
        return inst.Error.empty();
    }

    bool AddonHost::Call(Instance& inst, const std::string& method, const Value* args, int argc)
    {
        if (!inst.Loaded || !inst.VM)
            return false;
        auto* object = static_cast<CQS::CQSInstanceObject*>(inst.Object.ObjectVal);
        if (!object || !object->Class || !object->Class->FindMethod(method))
            return false;
        std::shared_ptr<Scene> scene = m_Hooks.GetScene ? m_Hooks.GetScene() : nullptr;
        EditorScriptScope scope(scene.get());
        bool found = false;
        bool ok = inst.VM->CallMethod(inst.Object, method, args, argc, nullptr, &found);
        if (!ok)
        {
            Value ex = inst.VM->TakeException();
            std::string message = inst.VM->GetLastError();
            if (message.empty()) message = ex.ToString();
            inst.Error = method + ": " + message;
            CQ_CORE_ERROR("[Addon:{0}] {1}", inst.Manifest.Name, inst.Error);
        }
        return ok;
    }

    void AddonHost::Run(Instance& inst, const std::string& method, const std::string& label)
    {
        auto action = [this, &inst, method]() { Call(inst, method); };
        if (m_Hooks.Undoable && !(m_Hooks.IsPlaying && m_Hooks.IsPlaying()))
            m_Hooks.Undoable(inst.Manifest.Name + ": " + label, action);
        else
            action();
    }

    bool AddonHost::Invoke(const std::string& id, const std::string& method)
    {
        Instance* inst = FindInstance(id);
        if (!inst || !inst->Loaded)
            return false;
        auto* object = static_cast<CQS::CQSInstanceObject*>(inst->Object.ObjectVal);
        if (!object || !object->Class || !object->Class->FindMethod(method))
            return false;
        Run(*inst, method, method);
        return inst->Error.empty();
    }

    bool AddonHost::OpenPanel(const std::string& id, const std::string& title)
    {
        Instance* inst = FindInstance(id);
        if (!inst)
            return false;
        for (auto& p : inst->Panels)
            if (p.Title == title)
            {
                p.Open = true;
                p.Focus = true;
                return true;
            }
        return false;
    }

    // ------------------------------------------------------------------ Kare

    void AddonHost::Update(float dt)
    {
        Sync();
        for (auto& inst : m_Instances)
            if (inst->Loaded && inst->HasUpdate)
            {
                Value arg = Value::MakeFloat(dt);
                Call(*inst, "OnEditorUpdate", &arg, 1);
            }
    }

    void AddonHost::DrawMenuItems()
    {
        bool any = false;
        for (auto& inst : m_Instances)
        {
            if (inst->Menu.empty() && inst->Panels.empty())
                continue;
            any = true;
            // "Kategori/Öğe" yolu: alt menüler
            for (auto& item : inst->Menu)
            {
                std::vector<std::string> parts;
                std::stringstream ss(item.Path);
                std::string part;
                while (std::getline(ss, part, '/'))
                    if (!part.empty()) parts.push_back(part);
                if (parts.empty())
                    continue;
                int opened = 0;
                bool visible = true;
                for (size_t i = 0; i + 1 < parts.size(); ++i)
                {
                    if (!ImGui::BeginMenu(parts[i].c_str()))
                    {
                        visible = false;
                        break;
                    }
                    ++opened;
                }
                if (visible && ImGui::MenuItem(parts.back().c_str()))
                    Run(*inst, item.Method, parts.back());
                for (int i = 0; i < opened; ++i)
                    ImGui::EndMenu();
            }
            for (auto& panel : inst->Panels)
            {
                std::string label = std::string(ICON_FA_WINDOW_MAXIMIZE "  ") + panel.Title;
                if (ImGui::MenuItem(label.c_str(), nullptr, panel.Open))
                {
                    panel.Open = !panel.Open;
                    panel.Focus = panel.Open;
                }
            }
        }
        if (!any)
            ImGui::TextDisabled("No addon menu items");
    }

    void AddonHost::DrawPanels()
    {
        for (auto& inst : m_Instances)
        {
            if (!inst->Loaded)
                continue;
            for (size_t i = 0; i < inst->Panels.size(); ++i)
            {
                Panel& panel = inst->Panels[i];
                if (!panel.Open)
                    continue;
                std::string title = std::string(UI::IconFromName(inst->Manifest.Icon)) + " " + panel.Title + "###Addon_" + inst->Manifest.Id + "_" + panel.Title;
                ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_FirstUseEver);
                if (unsigned int dock = m_Hooks.ToolsDockId ? m_Hooks.ToolsDockId() : 0u)
                    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
                if (panel.Focus)
                {
                    ImGui::SetNextWindowFocus();
                    panel.Focus = false;
                }
                bool open = true;
                if (ImGui::Begin(title.c_str(), &open))
                {
                    m_InPanel = true;
                    m_DisabledDepth = 0;
                    m_IndentDepth = 0;
                    const std::string method = panel.Method;  // çizim sırasında panel listesi değişebilir
                    Call(*inst, method);
                    // Betik dengesiz bıraktıysa ImGui yığınını düzelt
                    for (; m_DisabledDepth > 0; --m_DisabledDepth) ImGui::EndDisabled();
                    for (; m_IndentDepth > 0; --m_IndentDepth) ImGui::Unindent();
                    m_InPanel = false;
                    if (!inst->Error.empty())
                    {
                        ImGui::Separator();
                        UI::InfoBox(inst->Error.c_str(), UI::InfoType::Error);
                    }
                }
                ImGui::End();
                if (i < inst->Panels.size() && !open)
                    inst->Panels[i].Open = false;
            }
        }
    }

    // ------------------------------------------------------------------ Native API

    void AddonHost::RegisterNatives(Instance& inst)
    {
        CQS::CQSVM* vm = inst.VM.get();
        Instance* self = &inst;
        AddonHost* host = this;
        auto scene = [host]() -> Scene* { return host->m_Hooks.GetScene ? host->m_Hooks.GetScene().get() : nullptr; };

        // ---- Kayıt
        vm->DefineNative("EditorAddMenuItem", [self](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            self->Menu.push_back({ Arg(argc, args, 0), Arg(argc, args, 1) });
            return Value::MakeBool(true);
        }, 2);
        vm->DefineNative("EditorAddToolButton", [self](int argc, Value* args) -> Value {
            if (argc < 3) return Value::MakeBool(false);
            self->Buttons.push_back({ Arg(argc, args, 0), Arg(argc, args, 1, "puzzle-piece"), Arg(argc, args, 2), Arg(argc, args, 3) });
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("EditorAddPanel", [self](int argc, Value* args) -> Value {
            if (argc < 2) return Value::MakeBool(false);
            Panel p;
            p.Title = Arg(argc, args, 0);
            p.Method = Arg(argc, args, 1);
            p.Open = argc > 2 && args[2].IsTruthy();
            for (auto& existing : self->Panels)
                if (existing.Title == p.Title) { existing.Method = p.Method; return Value::MakeBool(true); }
            self->Panels.push_back(p);
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("EditorOpenPanel", [self](int argc, Value* args) -> Value {
            for (auto& p : self->Panels)
                if (p.Title == Arg(argc, args, 0)) { p.Open = true; p.Focus = true; }
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("EditorClosePanel", [self](int argc, Value* args) -> Value {
            for (auto& p : self->Panels)
                if (p.Title == Arg(argc, args, 0)) p.Open = false;
            return Value::MakeNull();
        }, 1);

        // ---- Editör durumu
        vm->DefineNative("EditorGetSelection", [host](int, Value*) -> Value {
            auto* list = new CQS::CQSListObject();
            if (host->m_Hooks.GetSelection)
                for (Entity e : host->m_Hooks.GetSelection())
                    if (e) list->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)e));
            return Value::MakeObject(list);
        }, 0);
        vm->DefineNative("EditorSetSelection", [host, scene](int argc, Value* args) -> Value {
            Scene* s = scene();
            if (!s || !host->m_Hooks.SetSelection) return Value::MakeNull();
            std::vector<Entity> sel;
            auto add = [&](const Value& v) {
                if (!v.IsNumber()) return;
                entt::entity h = (entt::entity)(uint32_t)(int64_t)v.ToNumber();
                if (s->m_Registry.valid(h)) sel.push_back(Entity{ h, s });
            };
            if (argc > 0 && args[0].IsList())
                for (auto& v : args[0].AsList()->Elements) add(v);
            else if (argc > 0)
                add(args[0]);
            host->m_Hooks.SetSelection(sel);
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("EditorFocus", [host, scene](int argc, Value* args) -> Value {
            Scene* s = scene();
            if (!s || argc < 1 || !args[0].IsNumber() || !host->m_Hooks.Focus) return Value::MakeNull();
            entt::entity h = (entt::entity)(uint32_t)(int64_t)args[0].ToNumber();
            if (s->m_Registry.valid(h)) host->m_Hooks.Focus(Entity{ h, s });
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("EditorNotify", [host, self](int argc, Value* args) -> Value {
            host->Notify(*self, Arg(argc, args, 0));
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("EditorIsPlaying", [host](int, Value*) -> Value {
            return Value::MakeBool(host->m_Hooks.IsPlaying && host->m_Hooks.IsPlaying());
        }, 0);
        vm->DefineNative("EditorGetCameraTarget", [host](int, Value*) -> Value {
            return Vec3Value(host->m_Hooks.GetCameraTarget ? host->m_Hooks.GetCameraTarget() : glm::vec3(0.0f));
        }, 0);
        vm->DefineNative("EditorGetCameraPosition", [host](int, Value*) -> Value {
            return Vec3Value(host->m_Hooks.GetCameraPosition ? host->m_Hooks.GetCameraPosition() : glm::vec3(0.0f));
        }, 0);
        vm->DefineNative("EditorGetAddonPath", [self](int, Value*) -> Value {
            return Value::MakeString(self->Manifest.Directory.generic_string());
        }, 0);
        vm->DefineNative("EditorGetProjectPath", [](int, Value*) -> Value {
            return Value::MakeString(Project::GetActiveProjectDirectory().generic_string());
        }, 0);
        vm->DefineNative("EditorRunUndoable", [host, self](int argc, Value* args) -> Value {
            // Panel düğmelerinden: metodu geri alınabilir eylem olarak çalıştır
            if (argc < 2) return Value::MakeBool(false);
            host->Run(*self, Arg(argc, args, 1), Arg(argc, args, 0));
            return Value::MakeBool(self->Error.empty());
        }, 2);
        vm->DefineNative("EditorEntityCount", [scene](int, Value*) -> Value {
            Scene* s = scene();
            return Value::MakeInt(s ? (int64_t)s->m_Registry.view<IDComponent>().size() : 0);
        }, 0);
        vm->DefineNative("EditorGetAllEntities", [scene](int, Value*) -> Value {
            auto* list = new CQS::CQSListObject();
            if (Scene* s = scene())
                s->m_Registry.view<IDComponent>().each([&](auto e, auto&) { list->Elements.push_back(Value::MakeInt((int64_t)(uint32_t)e)); });
            return Value::MakeObject(list);
        }, 0);

        // ---- Gui (yalnızca panel çiziminde)
        vm->DefineNative("GuiText", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel) ImGui::TextWrapped("%s", Arg(argc, args, 0).c_str());
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("GuiTextColored", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel)
                ImGui::TextColored(ImVec4((float)Num(argc, args, 1, 1), (float)Num(argc, args, 2, 1), (float)Num(argc, args, 3, 1), 1.0f), "%s",
                                   Arg(argc, args, 0).c_str());
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("GuiTextDisabled", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel) ImGui::TextDisabled("%s", Arg(argc, args, 0).c_str());
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("GuiHeader", [host](int argc, Value* args) -> Value {
            if (!host->m_InPanel) return Value::MakeBool(false);
            return Value::MakeBool(ImGui::CollapsingHeader(Arg(argc, args, 0).c_str(), argc > 1 && !args[1].IsTruthy() ? 0 : ImGuiTreeNodeFlags_DefaultOpen));
        }, -1);
        vm->DefineNative("GuiButton", [host](int argc, Value* args) -> Value {
            if (!host->m_InPanel) return Value::MakeBool(false);
            float w = (float)Num(argc, args, 1, 0.0);
            return Value::MakeBool(w < 0.0f ? UI::AccentButton(Arg(argc, args, 0).c_str(), ImVec2(-1, 0))
                                            : ImGui::Button(Arg(argc, args, 0).c_str(), ImVec2(w, 0)));
        }, -1);
        vm->DefineNative("GuiCheckbox", [host](int argc, Value* args) -> Value {
            bool v = argc > 1 && args[1].IsTruthy();
            if (host->m_InPanel) UI::Checkbox(Arg(argc, args, 0).c_str(), v);
            return Value::MakeBool(v);
        }, 2);
        vm->DefineNative("GuiSliderFloat", [host](int argc, Value* args) -> Value {
            float v = (float)Num(argc, args, 1);
            if (host->m_InPanel) UI::SliderFloat(Arg(argc, args, 0).c_str(), v, (float)Num(argc, args, 2, 0), (float)Num(argc, args, 3, 1));
            return Value::MakeFloat(v);
        }, 4);
        vm->DefineNative("GuiSliderInt", [host](int argc, Value* args) -> Value {
            int v = (int)Num(argc, args, 1);
            if (host->m_InPanel) UI::SliderInt(Arg(argc, args, 0).c_str(), v, (int)Num(argc, args, 2, 0), (int)Num(argc, args, 3, 100));
            return Value::MakeInt(v);
        }, 4);
        vm->DefineNative("GuiDragFloat", [host](int argc, Value* args) -> Value {
            float v = (float)Num(argc, args, 1);
            if (host->m_InPanel) UI::DragFloat(Arg(argc, args, 0).c_str(), v, (float)Num(argc, args, 2, 0.05));
            return Value::MakeFloat(v);
        }, -1);
        vm->DefineNative("GuiInputText", [host](int argc, Value* args) -> Value {
            std::string v = Arg(argc, args, 1);
            if (host->m_InPanel) UI::InputText(Arg(argc, args, 0).c_str(), v);
            return Value::MakeString(v);
        }, 2);
        vm->DefineNative("GuiColor", [host](int argc, Value* args) -> Value {
            glm::vec3 c((float)Num(argc, args, 1, 1), (float)Num(argc, args, 2, 1), (float)Num(argc, args, 3, 1));
            if (host->m_InPanel) UI::Color3(Arg(argc, args, 0).c_str(), c);
            auto* m = new CQS::CQSMapObject();
            m->Entries["r"] = Value::MakeFloat(c.r);
            m->Entries["g"] = Value::MakeFloat(c.g);
            m->Entries["b"] = Value::MakeFloat(c.b);
            return Value::MakeObject(m);
        }, 4);
        vm->DefineNative("GuiCombo", [host](int argc, Value* args) -> Value {
            int index = (int)Num(argc, args, 1);
            if (host->m_InPanel && argc > 2 && args[2].IsList())
            {
                std::vector<std::string> items;
                for (auto& v : args[2].AsList()->Elements) items.push_back(v.ToString());
                std::vector<const char*> ptrs;
                for (auto& s : items) ptrs.push_back(s.c_str());
                UI::Combo(Arg(argc, args, 0).c_str(), index, ptrs.data(), (int)ptrs.size());
            }
            return Value::MakeInt(index);
        }, 3);
        vm->DefineNative("GuiProgress", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel) ImGui::ProgressBar((float)Num(argc, args, 0), ImVec2(-1, 0), argc > 1 ? Arg(argc, args, 1).c_str() : nullptr);
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("GuiInfo", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel)
            {
                std::string kind = Arg(argc, args, 1, "info");
                UI::InfoBox(Arg(argc, args, 0).c_str(), kind == "warning" ? UI::InfoType::Warning : kind == "error" ? UI::InfoType::Error
                                                     : kind == "success" ? UI::InfoType::Success : UI::InfoType::Info);
            }
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("GuiSeparator", [host](int, Value*) -> Value { if (host->m_InPanel) ImGui::Separator(); return Value::MakeNull(); }, 0);
        vm->DefineNative("GuiSameLine", [host](int, Value*) -> Value { if (host->m_InPanel) ImGui::SameLine(); return Value::MakeNull(); }, 0);
        vm->DefineNative("GuiSpacing", [host](int, Value*) -> Value { if (host->m_InPanel) ImGui::Spacing(); return Value::MakeNull(); }, 0);
        vm->DefineNative("GuiIndent", [host](int, Value*) -> Value {
            if (host->m_InPanel) { ImGui::Indent(); ++host->m_IndentDepth; }
            return Value::MakeNull();
        }, 0);
        vm->DefineNative("GuiUnindent", [host](int, Value*) -> Value {
            if (host->m_InPanel && host->m_IndentDepth > 0) { ImGui::Unindent(); --host->m_IndentDepth; }
            return Value::MakeNull();
        }, 0);
        vm->DefineNative("GuiBeginDisabled", [host](int argc, Value* args) -> Value {
            if (host->m_InPanel) { ImGui::BeginDisabled(argc < 1 || args[0].IsTruthy()); ++host->m_DisabledDepth; }
            return Value::MakeNull();
        }, -1);
        vm->DefineNative("GuiEndDisabled", [host](int, Value*) -> Value {
            if (host->m_InPanel && host->m_DisabledDepth > 0) { ImGui::EndDisabled(); --host->m_DisabledDepth; }
            return Value::MakeNull();
        }, 0);
    }
}
