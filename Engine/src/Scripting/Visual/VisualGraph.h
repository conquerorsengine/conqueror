#pragma once

#include "Core/Base/Base.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Görsel betik (Blueprint benzeri) grafiği: düğümler, pinler, bağlantılar ve değişkenler.
// Grafik ConquerorScript koduna çevrilir (transpile) ve CQS çalışma zamanında (JIT) çalışır.
namespace Conqueror::Visual
{
    enum class PinType : int { Exec = 0, Any, Number, Bool, String, Vector, List, Dictionary };

    CQ_API const char* PinTypeName(PinType t);
    CQ_API PinType PinTypeFromName(const std::string& name);
    // Bağlanabilir mi (çıkış -> giriş)
    CQ_API bool PinsCompatible(PinType from, PinType to);

    struct PinDef
    {
        std::string Name;
        PinType     Type = PinType::Any;
        std::string Default;          // bağlı değilken kullanılan değer (CQS sabiti olarak), girişler için
    };

    enum class NodeKind : int
    {
        Event,       // olay (Start, Update, özel olay, tuşa basıldı ...)
        Flow,        // akış kontrolü (Branch, For, Sequence ...)
        Pure,        // yan etkisiz ifade (Add, GetPosition ...)
        Action,      // yan etkili çağrı (SetPosition, Print, değişken atama ...)
        Literal,     // sabit değer
        Variable,    // değişken okuma (pure)
        Comment
    };

    struct NodeDef
    {
        std::string Id;               // "flow.branch", "math.add", "native.SetPosition"
        std::string Title;
        std::string Category;         // arama menüsü kategorisi
        std::string Description;
        NodeKind    Kind = NodeKind::Pure;
        std::vector<PinDef> Inputs;   // exec pinleri dahil
        std::vector<PinDef> Outputs;
        // Pure/Action: çıktı ifadesi; {0}, {1} girişlerin ifadeleri (exec pinleri sayılmaz).
        std::string Expression;
        // Düğüm özellikleri (ör. özel olay adı, değişken adı, tuş)
        std::vector<std::pair<std::string, std::string>> Properties;
    };

    struct Node
    {
        int         Id = 0;
        std::string Type;             // NodeDef::Id
        float       X = 0.0f, Y = 0.0f;
        std::map<std::string, std::string> Properties;
        std::map<std::string, std::string> InputDefaults; // pin adı -> sabit
        // Yorum kutusu / yapışkan not boyutu
        float       Width = 0.0f, Height = 0.0f;
        // Unreal "Disable Node": düğüm hiçbir şey yapmaz, akış ilk exec çıkışından geçer (olaylar hiç çalışmaz)
        bool        Disabled = false;
    };

    struct Link
    {
        int Id = 0;
        int FromNode = 0;
        std::string FromPin;
        int ToNode = 0;
        std::string ToPin;
    };

    // Unity Visual Scripting değişken kapsamları + Unreal yerel (fonksiyon) değişkenleri
    // Graph: grafik örneğinin alanı; Object: varlığın tüm grafiklerince paylaşılır; Scene: sahne boyunca; Application: oyun boyunca
    // (oyun durunca sıfırlanır); Saved: PlayerPrefs ile oturumlar arası kalıcı; Local: yalnız Function'daki fonksiyonda
    enum class VariableScope : int { Graph = 0, Object, Scene, Application, Saved, Local };
    CQ_API const char* VariableScopeName(VariableScope s);
    CQ_API VariableScope VariableScopeFromName(const std::string& name);

    struct Variable
    {
        std::string Name;
        PinType     Type = PinType::Number;
        std::string Default;          // CQS sabiti
        bool        Public = true;
        std::string Tooltip;
        VariableScope Scope = VariableScope::Graph;
        std::string Function;         // Local: sahip fonksiyon
        std::string TypeAsset;        // yapı (.cqstruct) / sayım (.cqenum) türü (proje göreli yol)
    };

    // Unreal yer imi: görünüm (merkez ve yakınlaştırma) ve açık grafik (boş: olay grafiği, değilse makro adı)
    struct Bookmark
    {
        std::string Name;
        std::string Macro;
        float X = 0.0f, Y = 0.0f, Zoom = 1.0f;
    };

    class Graph;

    // Blueprint Macro (Unreal): grafiğe ait, birden çok exec girişi / çıkışı olabilen, her kullanıldığı yerde satır içi açılan alt
    // grafik. Gövdedeki "Inputs" (macro.inputs) ve "Outputs" (macro.outputs) tünel düğümlerinin "Pins" özelliği ("Ad:tip, ...",
    // tip: exec, number, bool, string, vector, list, any) makronun pinleridir. Makro düğümü (macro.instance) bu pinlerle görünür
    struct MacroDef
    {
        std::string Name;
        std::shared_ptr<Graph> Body;
    };

    struct CompileMessage
    {
        int NodeId = 0;               // 0: genel
        std::string Text;
        bool IsWarning = false;
    };

    class CQ_API Graph
    {
    public:
        std::string Name = "NewGraph";
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::vector<Variable> Variables;
        int NextId = 1;
        // Blueprint Macro'lar (bu grafiğin; gövdeleri ayrı grafikler)
        std::vector<MacroDef> Macros;
        // Makro gövdesi grafiğinde: makroları aranacak sahip grafik (iç içe makro düğümleri için; düzenleyici her karede bağlar)
        const Graph* MacroScope = nullptr;
        // Unreal Event Dispatcher adları (Call / Bind / Unbind düğümleri) ve uygulanan Blueprint Interface varlıkları (.cqinterface)
        std::vector<std::string> Dispatchers;
        std::vector<std::string> Interfaces;
        // İzlenen pinler (Unreal Watch this value): (düğüm, pin), çıkış pinleri
        std::vector<std::pair<int, std::string>> Watches;
        std::vector<Bookmark> Bookmarks;

        const MacroDef* FindMacro(const std::string& name) const;
        // Yeni makro: girişte "In" exec, çıkışta "Out" exec pinli tünel düğümleriyle
        MacroDef& AddMacro(const std::string& name);
        // Makronun tünel düğümlerinden pinleri (girişler = Inputs düğümünün çıkışları, çıkışlar = Outputs düğümünün girişleri)
        static void MacroPins(const Graph& body, std::vector<PinDef>& inputs, std::vector<PinDef>& outputs);
        // Makro örneklerinin gövdeleriyle (benzersiz kimliklerle, iç içe makrolar dahil) açıldığı düz grafik. expandedToInstance:
        // açılan düğüm -> en dıştaki makro düğümü (hata ve hata ayıklayıcı vurgusu makro düğümünde)
        Graph ExpandMacros(std::vector<CompileMessage>& messages, std::unordered_map<int, int>* expandedToInstance = nullptr) const;

        // Makro ya da alt grafik (Unity Subgraph) örneği var mı (derlemede ExpandMacros ile satır içi açılır)
        bool NeedsExpansion() const;

        Node* FindNode(int id);
        const Node* FindNode(int id) const;
        int AddNode(const std::string& type, float x, float y);
        void RemoveNode(int id);
        // Geçerliyse bağlantı kurar (giriş pinindeki eski bağlantıyı değiştirir; exec çıkışı tek bağlantılıdır)
        bool Connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin, std::string* error = nullptr);
        void Disconnect(int linkId);
        const Link* FindInputLink(int node, const std::string& pin) const;
        std::vector<const Link*> FindOutputLinks(int node, const std::string& pin) const;
        Variable* FindVariable(const std::string& name);
        const Variable* FindVariable(const std::string& name) const;

        // Düğümün tanımı (değişken düğümleri için değişken tipine göre özelleştirilmiş)
        NodeDef ResolveDef(const Node& node) const;

        std::string ToJson() const;
        bool FromJson(const std::string& json, std::string* error = nullptr);
        bool Save(const std::string& path) const;
        bool Load(const std::string& path, std::string* error = nullptr);

        // ConquerorScript koduna çevir. lineToNode: üretilen satır (1 tabanlı) -> düğüm.
        // trace: her yürütülen düğümden önce VSTrace(sınıf, düğüm) çağrısı (Blueprint hata ayıklayıcısı, editörde)
        std::string Transpile(const std::string& className, std::vector<CompileMessage>& messages, std::vector<int>* lineToNode = nullptr,
                              bool trace = false) const;

        // ── Düzenleme araçları (VisualGraphTools.cpp) ──
        // Olaylardan exec ile ulaşılan düğümler ve onların veri bağımlılıkları (Unity "Dim": geri kalanlar soluk)
        std::set<int> ReachableNodes() const;
        // Unreal Collapse to Function: seçim tek exec girişli / en fazla tek exec çıkışlı olmalı; dış veri girişleri fonksiyon girişi,
        // tek dış veri çıkışı Return Value olur. Seçim çağrı düğümüyle değişir (döner: çağrı düğümü, 0: hata)
        int CollapseToFunction(const std::set<int>& nodes, const std::string& name, std::string* error = nullptr);
        // Unreal Collapse to Macro: seçim makro gövdesine taşınır, dış bağlantılar tünel pinleri olur (döner: makro düğümü)
        int CollapseToMacro(const std::set<int>& nodes, const std::string& name, std::string* error = nullptr);
        // Unreal Promote to Variable: giriş pini -> Get düğümü, çıkış pini -> Set düğümü (döner: yeni düğüm)
        int PromoteToVariable(int node, const std::string& pin, bool output, const std::string& variable, std::string* error = nullptr);
        // Bağlantının ortasına yeniden yönlendirme (reroute) düğümü (döner: yeni düğüm)
        int InsertReroute(int linkId, float x, float y);
    };

    // Unreal Blueprint Diff: iki sürüm arasındaki düğüm / bağlantı / değişken farkları
    struct DiffEntry
    {
        enum class Kind : int { NodeAdded, NodeRemoved, NodeChanged, NodeMoved, LinkAdded, LinkRemoved, VariableAdded, VariableRemoved, VariableChanged, MacroAdded, MacroRemoved, MacroChanged };
        Kind        Type = Kind::NodeChanged;
        int         NodeId = 0;
        std::string Text;
    };
    CQ_API std::vector<DiffEntry> DiffGraphs(const Graph& base, const Graph& current);
    CQ_API const char* DiffKindName(DiffEntry::Kind kind);

    // Grafiklerde kullanılan kullanıcı türleri (Unreal User Defined Struct / Enum, Blueprint Interface) ve alt grafik dosyaları.
    // Yollar proje köküne göredir (Assets/...); dosya değişince yeniden okunur
    struct EnumAsset
    {
        std::string Name;
        std::vector<std::string> Entries;
    };
    struct StructField
    {
        std::string Name;
        PinType     Type = PinType::Number;
        std::string Default;
    };
    struct StructAsset
    {
        std::string Name;
        std::vector<StructField> Fields;
    };
    struct InterfaceFunction
    {
        std::string Name;
        std::vector<std::string> Inputs;
        bool HasReturn = false;
    };
    struct InterfaceAsset
    {
        std::string Name;
        std::vector<InterfaceFunction> Functions;
    };

    class CQ_API UserTypes
    {
    public:
        static std::string ResolvePath(const std::string& path);
        static const EnumAsset* LoadEnum(const std::string& path);
        static const StructAsset* LoadStruct(const std::string& path);
        static const InterfaceAsset* LoadInterface(const std::string& path);
        // Alt grafik dosyası (.cqgraph; "Inputs" / "Outputs" tünel düğümleri pinleridir)
        static std::shared_ptr<const Graph> LoadSubgraph(const std::string& path);
        static bool SaveEnum(const std::string& path, const EnumAsset& asset);
        static bool SaveStruct(const std::string& path, const StructAsset& asset);
        static bool SaveInterface(const std::string& path, const InterfaceAsset& asset);
        static void ClearCache();
        // Projedeki tüm varlıklar (uzantı: ".cqenum", ".cqstruct", ".cqinterface", ".cqgraph"); proje göreli yollar
        static std::vector<std::string> FindAssets(const std::string& extension);
        // Yapı / sayım varlığının adı (yoksa dosya adı)
        static std::string DisplayName(const std::string& path);
    };

    // Düğüm kitaplığı (yerleşik + nativebridge fonksiyonlarından otomatik)
    class CQ_API NodeLibrary
    {
    public:
        static const std::vector<NodeDef>& All();
        static const NodeDef* Find(const std::string& id);
    };

    // Geçerli bir CQS tanımlayıcısı (değişken/olay adı)
    CQ_API std::string SanitizeIdentifier(const std::string& name);

    // Blueprint hata ayıklayıcısı: editörde üretilen kod her yürütülen düğümde VSTrace çağırır; grafik paneli son çalışan düğümleri
    // ve bağlantıları vurgular, isabet sayısını gösterir. Kesme noktasına gelinince editör oyunu duraklatır (o karenin sonunda) ve
    // grafiği o düğümde açar. Grafik anahtarı: üretilen sınıf adı (dosya adı).
    class CQ_API Debugger
    {
    public:
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        static void RegisterGraph(const std::string& graph, const std::string& path);
        static std::string GetGraphPath(const std::string& graph);
        static void Trace(const std::string& graph, int node);
        // Son yürütme zamanı (s, Debugger saatine göre; hiç çalışmadıysa < 0) ve isabet sayısı
        static double LastExecuted(const std::string& graph, int node);
        static uint64_t HitCount(const std::string& graph, int node);
        static double Now();
        static void ClearHits();
        static void SetBreakpoint(const std::string& graph, int node, bool enabled);
        static bool HasBreakpoint(const std::string& graph, int node);
        // Kesme noktası tetiklendiyse bir kez true (editör duraklatır)
        static bool ConsumeBreak(std::string& graph, int& node);
        // Canlı pin değerleri (Unity "Values", Unreal Watch): üretilen kod VSWatch ile kaydeder; yalnız yakalama açıkken
        // (düzenleyici: Values açık, izlenen pin ya da izleme penceresi varken) metne çevrilir
        static void SetCaptureValues(bool capture);
        static bool IsCapturingValues();
        static void RecordValue(const std::string& graph, int node, const std::string& pin, const std::string& value);
        static bool GetValue(const std::string& graph, int node, const std::string& pin, std::string& value, double* time = nullptr);
        // Kesme noktasına gelinceye kadar yürütülen son düğümler (eskiden yeniye; çağrı yığını penceresi)
        static std::vector<std::pair<std::string, int>> GetBreakTrail();
    };

    // Oyun oturumu durumu: Event Dispatcher bağları (oyun başlarken / dururken temizlenir)
    class CQ_API DispatcherRegistry
    {
    public:
        struct Binding
        {
            uint32_t Entity = 0;
            std::string Method;
        };
        static void Bind(uint32_t owner, const std::string& dispatcher, uint32_t entity, const std::string& method);
        static void Unbind(uint32_t owner, const std::string& dispatcher, uint32_t entity, const std::string& method);
        static void UnbindAll(uint32_t owner, const std::string& dispatcher);
        static std::vector<Binding> Get(uint32_t owner, const std::string& dispatcher);
        static void Clear();
    };
}
