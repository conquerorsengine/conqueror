#pragma once

#include "Core/Base/Base.h"

#include <map>
#include <string>
#include <vector>

// Düğüm grafiği varlıkları için ortak dosya biçimi (State Graph, Behavior Tree, Sound Cue, PCG Graph ...):
// türü dosyadaki "NodeAsset" alanıyla belli olan düğümler, özellikleri ve aralarındaki bağlantılar. Çalışma zamanı
// kendi türünün anlamını okur; düzenleyicide hepsi aynı genel panelle açılır.
namespace Conqueror::NodeAssets
{
    struct Node
    {
        int Id = 0;
        std::string Type;
        float X = 0.0f, Y = 0.0f;
        std::map<std::string, std::string> Props;

        CQ_API std::string Str(const std::string& key, const std::string& def = "") const;
        CQ_API float Float(const std::string& key, float def = 0.0f) const;
        CQ_API int Int(const std::string& key, int def = 0) const;
        CQ_API bool Bool(const std::string& key, bool def = false) const;
    };

    struct Link
    {
        int Id = 0;
        int From = 0;
        int FromPin = 0;
        int To = 0;
        int ToPin = 0;
    };

    struct CQ_API File
    {
        std::string Kind;
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        int NextId = 1;

        Node* Find(int id);
        const Node* Find(int id) const;
        int Add(const std::string& type, float x, float y);
        void Remove(int id);
        bool Connect(int from, int fromPin, int to, int toPin);
        void Disconnect(int linkId);
        // pin < 0: düğümün tüm çıkış / giriş bağlantıları
        std::vector<const Link*> Out(int node, int pin = -1) const;
        std::vector<const Link*> In(int node, int pin = -1) const;
        std::string ToText() const;
        bool FromText(const std::string& text);
    };

    // Yol proje göreli olabilir; dosya değişmedikçe önbellekten döner
    CQ_API bool Load(const std::string& path, File& out);
    CQ_API bool Save(const std::string& path, const File& file);
    // Dosyanın "NodeAsset" türü (yoksa boş)
    CQ_API std::string KindOf(const std::string& path);
}
