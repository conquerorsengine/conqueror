#pragma once

#include "Core/Base/Base.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Görsel betik (Blueprint benzeri) grafiği: düğümler, pinler, bağlantılar ve değişkenler.
// Grafik ConquerorScript koduna çevrilir (transpile) ve CQS çalışma zamanında (JIT) çalışır.
namespace Conqueror::Visual
{
    enum class PinType : int { Exec = 0, Any, Number, Bool, String, Vector, List };

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
        // Yorum düğümü
        float       Width = 0.0f, Height = 0.0f;
    };

    struct Link
    {
        int Id = 0;
        int FromNode = 0;
        std::string FromPin;
        int ToNode = 0;
        std::string ToPin;
    };

    struct Variable
    {
        std::string Name;
        PinType     Type = PinType::Number;
        std::string Default;          // CQS sabiti
        bool        Public = true;
        std::string Tooltip;
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
    };
}
