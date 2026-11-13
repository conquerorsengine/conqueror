#include "StatusIndicator.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/FileSystem.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/Signals.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>
#include "stb_image.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#if (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)) && \
    defined(CQ_HAS_DBUS_HEADERS) && !defined(__ANDROID__)
    #define CQ_STATUS_SNI 1
    #include <dbus/dbus.h>
    #include <dlfcn.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #define CQ_STATUS_WIN32 1
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace Conqueror
{
    namespace
    {
        // Simge: RGBA (satır satır, üstten), en çok 64 piksel
        struct IconImage
        {
            int Width = 0, Height = 0;
            std::vector<unsigned char> Rgba;
        };

        IconImage LoadIcon(const std::string& path)
        {
            IconImage icon;
            std::string file = path.empty() ? FileSystem::ResolveResourcePath("Resources/Logo/AppIcon/conqueror_256.png")
                                            : SceneSerializer::ResolveAssetPath(path);
            int w = 0, h = 0, n = 0;
            unsigned char* px = stbi_load(file.c_str(), &w, &h, &n, 4);
            if (!px)
            {
                // Dosya yoksa düz bir kare (tepside yine görünür)
                icon.Width = icon.Height = 32;
                icon.Rgba.assign(32 * 32 * 4, 0);
                for (size_t i = 0; i < icon.Rgba.size(); i += 4)
                {
                    icon.Rgba[i] = 70; icon.Rgba[i + 1] = 140; icon.Rgba[i + 2] = 230; icon.Rgba[i + 3] = 255;
                }
                return icon;
            }
            const int size = std::min(64, std::max(w, h));
            icon.Width = std::max(1, w * size / std::max(w, h));
            icon.Height = std::max(1, h * size / std::max(w, h));
            icon.Rgba.resize((size_t)icon.Width * icon.Height * 4);
            // Kutu süzgeciyle küçült
            for (int y = 0; y < icon.Height; ++y)
                for (int x = 0; x < icon.Width; ++x)
                {
                    const int x0 = x * w / icon.Width, x1 = std::max(x0 + 1, (x + 1) * w / icon.Width);
                    const int y0 = y * h / icon.Height, y1 = std::max(y0 + 1, (y + 1) * h / icon.Height);
                    unsigned sum[4] = { 0, 0, 0, 0 }, count = 0;
                    for (int sy = y0; sy < y1; ++sy)
                        for (int sx = x0; sx < x1; ++sx)
                        {
                            const unsigned char* p = px + ((size_t)sy * w + sx) * 4;
                            for (int k = 0; k < 4; ++k) sum[k] += p[k];
                            ++count;
                        }
                    unsigned char* o = &icon.Rgba[((size_t)y * icon.Width + x) * 4];
                    for (int k = 0; k < 4; ++k) o[k] = (unsigned char)(sum[k] / std::max(1u, count));
                }
            stbi_image_free(px);
            return icon;
        }

        struct PendingEvent
        {
            int Kind = 0;      // 0 pressed, 1 menu item
            int Button = 0;
            int X = 0, Y = 0;
            int Item = -1;
        };
    }

#if defined(CQ_STATUS_SNI)
    // ================================================================ Linux / BSD: D-Bus StatusNotifierItem + dbusmenu
    namespace
    {
        // libdbus çalışma zamanında (dlopen): kurulu değilse özellik kapanır, motor yine çalışır
        #define CQ_DBUS_FUNCTIONS(X) \
            X(dbus_error_init) X(dbus_error_free) X(dbus_error_is_set) \
            X(dbus_bus_get_private) X(dbus_bus_request_name) \
            X(dbus_connection_set_exit_on_disconnect) X(dbus_connection_register_object_path) X(dbus_connection_add_filter) \
            X(dbus_connection_read_write) X(dbus_connection_dispatch) X(dbus_connection_close) X(dbus_connection_unref) \
            X(dbus_connection_flush) X(dbus_connection_send) X(dbus_connection_pop_message) \
            X(dbus_message_new_method_call) X(dbus_message_new_method_return) X(dbus_message_new_error) X(dbus_message_new_signal) \
            X(dbus_message_unref) X(dbus_message_get_interface) X(dbus_message_get_member) X(dbus_message_get_path) \
            X(dbus_message_get_type) X(dbus_message_get_reply_serial) \
            X(dbus_message_iter_init) X(dbus_message_iter_init_append) X(dbus_message_iter_append_basic) \
            X(dbus_message_iter_open_container) X(dbus_message_iter_close_container) X(dbus_message_iter_get_arg_type) \
            X(dbus_message_iter_get_basic) X(dbus_message_iter_next) X(dbus_message_iter_recurse) X(dbus_message_iter_append_fixed_array)

        struct DBusApi
        {
            bool Ok = false;
            #define CQ_DBUS_MEMBER(name) decltype(&::name) name = nullptr;
            CQ_DBUS_FUNCTIONS(CQ_DBUS_MEMBER)
            #undef CQ_DBUS_MEMBER
        };

        DBusApi& Api()
        {
            static DBusApi api = [] {
                DBusApi a;
                void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
                if (!lib) lib = dlopen("libdbus-1.so", RTLD_NOW | RTLD_LOCAL);
                if (!lib)
                    return a;
                bool ok = true;
                #define CQ_DBUS_LOAD(name) a.name = reinterpret_cast<decltype(a.name)>(dlsym(lib, #name)); ok = ok && a.name;
                CQ_DBUS_FUNCTIONS(CQ_DBUS_LOAD)
                #undef CQ_DBUS_LOAD
                a.Ok = ok;
                return a;
            }();
            return api;
        }

        struct SniItem
        {
            DBusConnection* Conn = nullptr;
            std::string Service;
            std::string Tooltip;
            bool Visible = true;
            IconImage Icon;
            std::string IconPath;
            std::vector<std::string> Menu;
            uint32_t MenuRevision = 1;
            dbus_uint32_t RegisterSerial = 0;
            bool Registered = false;
            bool RegisterFailed = false;
            std::vector<PendingEvent> Events;
        };

        // ---------------------------------------------------------------- yazma yardımcıları
        void Str(DBusMessageIter* it, const std::string& s)
        {
            const char* c = s.c_str();
            Api().dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &c);
        }
        void ObjPath(DBusMessageIter* it, const char* p) { Api().dbus_message_iter_append_basic(it, DBUS_TYPE_OBJECT_PATH, &p); }
        void Int(DBusMessageIter* it, int32_t v) { dbus_int32_t x = v; Api().dbus_message_iter_append_basic(it, DBUS_TYPE_INT32, &x); }
        void UInt(DBusMessageIter* it, uint32_t v) { dbus_uint32_t x = v; Api().dbus_message_iter_append_basic(it, DBUS_TYPE_UINT32, &x); }
        void Bool(DBusMessageIter* it, bool v) { dbus_bool_t x = v ? TRUE : FALSE; Api().dbus_message_iter_append_basic(it, DBUS_TYPE_BOOLEAN, &x); }

        template<typename F>
        void Container(DBusMessageIter* it, int type, const char* signature, F&& body)
        {
            DBusMessageIter sub;
            Api().dbus_message_iter_open_container(it, type, signature, &sub);
            body(&sub);
            Api().dbus_message_iter_close_container(it, &sub);
        }

        // a(iiay): ARGB32, ağ bayt sırası
        void Pixmap(DBusMessageIter* it, const IconImage& icon)
        {
            Container(it, DBUS_TYPE_ARRAY, "(iiay)", [&](DBusMessageIter* arr) {
                if (icon.Rgba.empty())
                    return;
                Container(arr, DBUS_TYPE_STRUCT, nullptr, [&](DBusMessageIter* st) {
                    Int(st, icon.Width);
                    Int(st, icon.Height);
                    std::vector<unsigned char> argb(icon.Rgba.size());
                    for (size_t i = 0; i + 3 < icon.Rgba.size(); i += 4)
                    {
                        argb[i] = icon.Rgba[i + 3];
                        argb[i + 1] = icon.Rgba[i];
                        argb[i + 2] = icon.Rgba[i + 1];
                        argb[i + 3] = icon.Rgba[i + 2];
                    }
                    Container(st, DBUS_TYPE_ARRAY, "y", [&](DBusMessageIter* bytes) {
                        const unsigned char* data = argb.data();
                        Api().dbus_message_iter_append_fixed_array(bytes, DBUS_TYPE_BYTE, &data, (int)argb.size());
                    });
                });
            });
        }

        bool ItemProperty(DBusMessageIter* it, const SniItem& item, const std::string& name)
        {
            auto variant = [&](const char* sig, auto&& body) { Container(it, DBUS_TYPE_VARIANT, sig, body); };
            if (name == "Category") variant("s", [&](DBusMessageIter* v) { Str(v, "ApplicationStatus"); });
            else if (name == "Id") variant("s", [&](DBusMessageIter* v) { Str(v, "conqueror-game"); });
            else if (name == "Title") variant("s", [&](DBusMessageIter* v) { Str(v, item.Tooltip); });
            else if (name == "Status") variant("s", [&](DBusMessageIter* v) { Str(v, item.Visible ? "Active" : "Passive"); });
            else if (name == "WindowId") variant("i", [&](DBusMessageIter* v) { Int(v, 0); });
            else if (name == "IconName" || name == "OverlayIconName" || name == "AttentionIconName" || name == "AttentionMovieName" || name == "IconThemePath")
                variant("s", [&](DBusMessageIter* v) { Str(v, ""); });
            else if (name == "IconPixmap") variant("a(iiay)", [&](DBusMessageIter* v) { Pixmap(v, item.Icon); });
            else if (name == "OverlayIconPixmap" || name == "AttentionIconPixmap") variant("a(iiay)", [&](DBusMessageIter* v) { Pixmap(v, IconImage{}); });
            else if (name == "ToolTip")
                variant("(sa(iiay)ss)", [&](DBusMessageIter* v) {
                    Container(v, DBUS_TYPE_STRUCT, nullptr, [&](DBusMessageIter* st) {
                        Str(st, "");
                        Pixmap(st, IconImage{});
                        Str(st, item.Tooltip);
                        Str(st, "");
                    });
                });
            else if (name == "ItemIsMenu") variant("b", [&](DBusMessageIter* v) { Bool(v, false); });
            else if (name == "Menu") variant("o", [&](DBusMessageIter* v) { ObjPath(v, item.Menu.empty() ? "/NO_DBUSMENU" : "/MenuBar"); });
            else return false;
            return true;
        }
        const char* kItemProperties[] = { "Category", "Id", "Title", "Status", "WindowId", "IconName", "IconPixmap", "OverlayIconName",
                                          "OverlayIconPixmap", "AttentionIconName", "AttentionIconPixmap", "AttentionMovieName", "ToolTip",
                                          "ItemIsMenu", "Menu" };

        bool MenuProperty(DBusMessageIter* it, const std::string& name)
        {
            auto variant = [&](const char* sig, auto&& body) { Container(it, DBUS_TYPE_VARIANT, sig, body); };
            if (name == "Version") variant("u", [&](DBusMessageIter* v) { UInt(v, 3); });
            else if (name == "TextDirection") variant("s", [&](DBusMessageIter* v) { Str(v, "ltr"); });
            else if (name == "Status") variant("s", [&](DBusMessageIter* v) { Str(v, "normal"); });
            else if (name == "IconThemePath") variant("as", [&](DBusMessageIter* v) { Container(v, DBUS_TYPE_ARRAY, "s", [](DBusMessageIter*) {}); });
            else return false;
            return true;
        }
        const char* kMenuProperties[] = { "Version", "TextDirection", "Status", "IconThemePath" };

        // Menü öğesinin özellikleri (a{sv}); 0 kök
        void MenuItemProps(DBusMessageIter* it, const SniItem& item, int id)
        {
            Container(it, DBUS_TYPE_ARRAY, "{sv}", [&](DBusMessageIter* dict) {
                auto entry = [&](const char* key, const char* sig, auto&& body) {
                    Container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, [&](DBusMessageIter* e) {
                        Str(e, key);
                        Container(e, DBUS_TYPE_VARIANT, sig, body);
                    });
                };
                if (id == 0)
                {
                    entry("children-display", "s", [](DBusMessageIter* v) { Str(v, "submenu"); });
                    return;
                }
                const std::string& label = item.Menu[(size_t)id - 1];
                if (label == "-")
                {
                    entry("type", "s", [](DBusMessageIter* v) { Str(v, "separator"); });
                    return;
                }
                entry("label", "s", [&](DBusMessageIter* v) { Str(v, label); });
                entry("enabled", "b", [](DBusMessageIter* v) { Bool(v, true); });
                entry("visible", "b", [](DBusMessageIter* v) { Bool(v, true); });
            });
        }

        void MenuLayout(DBusMessageIter* it, const SniItem& item, int id, bool children)
        {
            Container(it, DBUS_TYPE_STRUCT, nullptr, [&](DBusMessageIter* st) {
                Int(st, id);
                MenuItemProps(st, item, id);
                Container(st, DBUS_TYPE_ARRAY, "v", [&](DBusMessageIter* arr) {
                    if (id != 0 || !children)
                        return;
                    for (int i = 0; i < (int)item.Menu.size(); ++i)
                        Container(arr, DBUS_TYPE_VARIANT, "(ia{sv}av)", [&](DBusMessageIter* v) { MenuLayout(v, item, i + 1, false); });
                });
            });
        }

        // ---------------------------------------------------------------- okuma yardımcıları
        std::string ReadString(DBusMessageIter* it)
        {
            if (Api().dbus_message_iter_get_arg_type(it) != DBUS_TYPE_STRING)
                return {};
            const char* s = nullptr;
            Api().dbus_message_iter_get_basic(it, &s);
            return s ? s : "";
        }
        int32_t ReadInt(DBusMessageIter* it)
        {
            if (Api().dbus_message_iter_get_arg_type(it) != DBUS_TYPE_INT32)
                return 0;
            dbus_int32_t v = 0;
            Api().dbus_message_iter_get_basic(it, &v);
            return v;
        }

        const char* kItemIntrospection =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
            "<node><interface name=\"org.kde.StatusNotifierItem\">"
            "<property name=\"Category\" type=\"s\" access=\"read\"/><property name=\"Id\" type=\"s\" access=\"read\"/>"
            "<property name=\"Title\" type=\"s\" access=\"read\"/><property name=\"Status\" type=\"s\" access=\"read\"/>"
            "<property name=\"WindowId\" type=\"i\" access=\"read\"/><property name=\"IconName\" type=\"s\" access=\"read\"/>"
            "<property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/><property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
            "<property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/><property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
            "<property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/><property name=\"AttentionMovieName\" type=\"s\" access=\"read\"/>"
            "<property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/><property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
            "<property name=\"Menu\" type=\"o\" access=\"read\"/>"
            "<method name=\"ContextMenu\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
            "<method name=\"Activate\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
            "<method name=\"SecondaryActivate\"><arg name=\"x\" type=\"i\" direction=\"in\"/><arg name=\"y\" type=\"i\" direction=\"in\"/></method>"
            "<method name=\"Scroll\"><arg name=\"delta\" type=\"i\" direction=\"in\"/><arg name=\"orientation\" type=\"s\" direction=\"in\"/></method>"
            "<signal name=\"NewTitle\"/><signal name=\"NewIcon\"/><signal name=\"NewToolTip\"/><signal name=\"NewStatus\"><arg name=\"status\" type=\"s\"/></signal>"
            "</interface><interface name=\"org.freedesktop.DBus.Properties\">"
            "<method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>"
            "<method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>"
            "</interface></node>";
        const char* kMenuIntrospection =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
            "<node><interface name=\"com.canonical.dbusmenu\">"
            "<property name=\"Version\" type=\"u\" access=\"read\"/><property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
            "<property name=\"Status\" type=\"s\" access=\"read\"/><property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
            "<method name=\"GetLayout\"><arg type=\"i\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/>"
            "<arg type=\"u\" direction=\"out\"/><arg type=\"(ia{sv}av)\" direction=\"out\"/></method>"
            "<method name=\"GetGroupProperties\"><arg type=\"ai\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/><arg type=\"a(ia{sv})\" direction=\"out\"/></method>"
            "<method name=\"GetProperty\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>"
            "<method name=\"Event\"><arg type=\"i\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/></method>"
            "<method name=\"EventGroup\"><arg type=\"a(isvu)\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/></method>"
            "<method name=\"AboutToShow\"><arg type=\"i\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>"
            "<method name=\"AboutToShowGroup\"><arg type=\"ai\" direction=\"in\"/><arg type=\"ai\" direction=\"out\"/><arg type=\"ai\" direction=\"out\"/></method>"
            "<signal name=\"LayoutUpdated\"><arg type=\"u\"/><arg type=\"i\"/></signal>"
            "</interface></node>";

        DBusHandlerResult HandleMessage(DBusConnection* conn, DBusMessage* msg, void* data)
        {
            auto& A = Api();
            auto* item = static_cast<SniItem*>(data);
            if (A.dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            const std::string iface = A.dbus_message_get_interface(msg) ? A.dbus_message_get_interface(msg) : "";
            const std::string member = A.dbus_message_get_member(msg) ? A.dbus_message_get_member(msg) : "";
            const std::string path = A.dbus_message_get_path(msg) ? A.dbus_message_get_path(msg) : "";
            const bool menu = path == "/MenuBar";
            DBusMessage* reply = nullptr;
            DBusMessageIter args, out;
            const bool hasArgs = A.dbus_message_iter_init(msg, &args);
            auto ret = [&]() { reply = A.dbus_message_new_method_return(msg); A.dbus_message_iter_init_append(reply, &out); };

            if (iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect")
            {
                ret();
                Str(&out, menu ? kMenuIntrospection : kItemIntrospection);
            }
            else if (iface == "org.freedesktop.DBus.Properties" && hasArgs)
            {
                ReadString(&args);   // arayüz
                A.dbus_message_iter_next(&args);
                if (member == "Get")
                {
                    const std::string name = ReadString(&args);
                    ret();
                    const bool ok = menu ? MenuProperty(&out, name) : ItemProperty(&out, *item, name);
                    if (!ok)
                    {
                        A.dbus_message_unref(reply);
                        reply = A.dbus_message_new_error(msg, "org.freedesktop.DBus.Error.UnknownProperty", name.c_str());
                    }
                }
                else if (member == "GetAll")
                {
                    ret();
                    Container(&out, DBUS_TYPE_ARRAY, "{sv}", [&](DBusMessageIter* dict) {
                        auto add = [&](const char* name) {
                            Container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, [&](DBusMessageIter* e) {
                                Str(e, name);
                                if (menu) MenuProperty(e, name); else ItemProperty(e, *item, name);
                            });
                        };
                        if (menu) for (const char* n : kMenuProperties) add(n);
                        else for (const char* n : kItemProperties) add(n);
                    });
                }
                else
                    reply = A.dbus_message_new_error(msg, "org.freedesktop.DBus.Error.PropertyReadOnly", "read only");
            }
            else if (!menu && iface == "org.kde.StatusNotifierItem")
            {
                PendingEvent e;
                if (hasArgs)
                {
                    e.X = ReadInt(&args);
                    A.dbus_message_iter_next(&args);
                    e.Y = ReadInt(&args);
                }
                if (member == "Activate") e.Button = 1;
                else if (member == "ContextMenu") e.Button = 2;
                else if (member == "SecondaryActivate") e.Button = 3;
                if (e.Button)
                    item->Events.push_back(e);
                ret();
            }
            else if (menu && iface == "com.canonical.dbusmenu")
            {
                if (member == "GetLayout")
                {
                    const int parent = hasArgs ? ReadInt(&args) : 0;
                    ret();
                    UInt(&out, item->MenuRevision);
                    MenuLayout(&out, *item, parent >= 0 && parent <= (int)item->Menu.size() ? parent : 0, true);
                }
                else if (member == "GetGroupProperties")
                {
                    std::vector<int> ids;
                    if (hasArgs && A.dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
                    {
                        DBusMessageIter arr;
                        A.dbus_message_iter_recurse(&args, &arr);
                        while (A.dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_INT32)
                        {
                            ids.push_back(ReadInt(&arr));
                            A.dbus_message_iter_next(&arr);
                        }
                    }
                    if (ids.empty())
                        for (int i = 0; i <= (int)item->Menu.size(); ++i) ids.push_back(i);
                    ret();
                    Container(&out, DBUS_TYPE_ARRAY, "(ia{sv})", [&](DBusMessageIter* arr) {
                        for (int id : ids)
                            if (id >= 0 && id <= (int)item->Menu.size())
                                Container(arr, DBUS_TYPE_STRUCT, nullptr, [&](DBusMessageIter* st) {
                                    Int(st, id);
                                    MenuItemProps(st, *item, id);
                                });
                    });
                }
                else if (member == "GetProperty")
                {
                    const int id = hasArgs ? ReadInt(&args) : 0;
                    A.dbus_message_iter_next(&args);
                    const std::string name = ReadString(&args);
                    ret();
                    const std::string label = id >= 1 && id <= (int)item->Menu.size() ? item->Menu[(size_t)id - 1] : std::string();
                    Container(&out, DBUS_TYPE_VARIANT, name == "label" || name == "type" ? "s" : "b", [&](DBusMessageIter* v) {
                        if (name == "label") Str(v, label);
                        else if (name == "type") Str(v, label == "-" ? "separator" : "standard");
                        else Bool(v, true);
                    });
                }
                else if (member == "Event" || member == "EventGroup")
                {
                    auto clicked = [&](int id) {
                        if (id >= 1 && id <= (int)item->Menu.size() && item->Menu[(size_t)id - 1] != "-")
                        {
                            PendingEvent e;
                            e.Kind = 1;
                            e.Item = id - 1;
                            item->Events.push_back(e);
                        }
                    };
                    if (member == "Event" && hasArgs)
                    {
                        const int id = ReadInt(&args);
                        A.dbus_message_iter_next(&args);
                        if (ReadString(&args) == "clicked")
                            clicked(id);
                        ret();
                    }
                    else
                    {
                        if (hasArgs && A.dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
                        {
                            DBusMessageIter arr;
                            A.dbus_message_iter_recurse(&args, &arr);
                            while (A.dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT)
                            {
                                DBusMessageIter st;
                                A.dbus_message_iter_recurse(&arr, &st);
                                const int id = ReadInt(&st);
                                A.dbus_message_iter_next(&st);
                                if (ReadString(&st) == "clicked")
                                    clicked(id);
                                A.dbus_message_iter_next(&arr);
                            }
                        }
                        ret();
                        Container(&out, DBUS_TYPE_ARRAY, "i", [](DBusMessageIter*) {});
                    }
                }
                else if (member == "AboutToShow")
                {
                    ret();
                    Bool(&out, false);
                }
                else if (member == "AboutToShowGroup")
                {
                    ret();
                    Container(&out, DBUS_TYPE_ARRAY, "i", [](DBusMessageIter*) {});
                    Container(&out, DBUS_TYPE_ARRAY, "i", [](DBusMessageIter*) {});
                }
            }
            if (!reply)
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            A.dbus_connection_send(conn, reply, nullptr);
            A.dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        // Tepsi barındırıcısının (StatusNotifierWatcher) kayıt yanıtı
        DBusHandlerResult Filter(DBusConnection*, DBusMessage* msg, void* data)
        {
            auto& A = Api();
            auto* item = static_cast<SniItem*>(data);
            const int type = A.dbus_message_get_type(msg);
            if ((type == DBUS_MESSAGE_TYPE_METHOD_RETURN || type == DBUS_MESSAGE_TYPE_ERROR) && item->RegisterSerial &&
                A.dbus_message_get_reply_serial(msg) == item->RegisterSerial)
            {
                item->Registered = type == DBUS_MESSAGE_TYPE_METHOD_RETURN;
                item->RegisterFailed = !item->Registered;
                if (item->RegisterFailed)
                    CQ_CORE_WARN("[Status Indicator] No system tray host (StatusNotifierWatcher) on this desktop; the icon is not shown. "
                                 "On GNOME install the AppIndicator extension.");
                item->RegisterSerial = 0;
            }
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        void SendSignal(SniItem& item, const char* path, const char* iface, const char* name, const char* status = nullptr)
        {
            auto& A = Api();
            DBusMessage* s = A.dbus_message_new_signal(path, iface, name);
            if (!s)
                return;
            DBusMessageIter it;
            A.dbus_message_iter_init_append(s, &it);
            if (status)
                Str(&it, status);
            if (std::string(name) == "LayoutUpdated")
            {
                UInt(&it, item.MenuRevision);
                Int(&it, 0);
            }
            A.dbus_connection_send(item.Conn, s, nullptr);
            A.dbus_message_unref(s);
        }

        SniItem* CreateItem(const StatusIndicatorComponent& c)
        {
            auto& A = Api();
            if (!A.Ok)
            {
                static bool warned = false;
                if (!warned) CQ_CORE_WARN("[Status Indicator] libdbus-1 is not installed; tray icons are off");
                warned = true;
                return nullptr;
            }
            DBusError err;
            A.dbus_error_init(&err);
            DBusConnection* conn = A.dbus_bus_get_private(DBUS_BUS_SESSION, &err);
            if (!conn)
            {
                CQ_CORE_WARN("[Status Indicator] No D-Bus session bus: {0}", A.dbus_error_is_set(&err) ? err.message : "");
                A.dbus_error_free(&err);
                return nullptr;
            }
            A.dbus_connection_set_exit_on_disconnect(conn, FALSE);
            static int counter = 0;
            auto* item = new SniItem();
            item->Conn = conn;
            item->Service = "org.kde.StatusNotifierItem-" + std::to_string((long)getpid()) + "-" + std::to_string(++counter);
            A.dbus_bus_request_name(conn, item->Service.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
            if (A.dbus_error_is_set(&err))
            {
                CQ_CORE_WARN("[Status Indicator] Cannot own {0}: {1}", item->Service, err.message);
                A.dbus_error_free(&err);
            }
            item->Tooltip = c.Tooltip;
            item->Visible = c.Visible;
            item->Menu = c.MenuItems;
            item->IconPath = c.Icon;
            item->Icon = LoadIcon(c.Icon);
            static DBusObjectPathVTable vtable = [] {
                DBusObjectPathVTable v{};
                v.message_function = &HandleMessage;
                return v;
            }();
            A.dbus_connection_register_object_path(conn, "/StatusNotifierItem", &vtable, item);
            A.dbus_connection_register_object_path(conn, "/MenuBar", &vtable, item);
            A.dbus_connection_add_filter(conn, &Filter, item, nullptr);
            if (DBusMessage* m = A.dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                               "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem"))
            {
                DBusMessageIter it;
                A.dbus_message_iter_init_append(m, &it);
                Str(&it, item->Service);
                A.dbus_connection_send(conn, m, &item->RegisterSerial);
                A.dbus_message_unref(m);
            }
            A.dbus_connection_flush(conn);
            return item;
        }

        void DestroyItem(SniItem* item)
        {
            if (!item)
                return;
            auto& A = Api();
            if (item->Conn)
            {
                A.dbus_connection_close(item->Conn);
                A.dbus_connection_unref(item->Conn);
            }
            delete item;
        }

        // Bileşenden değişenleri yay, gelen istekleri işle
        void Pump(SniItem& item, StatusIndicatorComponent& c)
        {
            auto& A = Api();
            if (c.Tooltip != item.Tooltip)
            {
                item.Tooltip = c.Tooltip;
                SendSignal(item, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewToolTip");
                SendSignal(item, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewTitle");
            }
            if (c.Visible != item.Visible)
            {
                item.Visible = c.Visible;
                SendSignal(item, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewStatus", c.Visible ? "Active" : "Passive");
            }
            if (c.Icon != item.IconPath)
            {
                item.IconPath = c.Icon;
                item.Icon = LoadIcon(c.Icon);
                SendSignal(item, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewIcon");
            }
            if (c.MenuItems != item.Menu)
            {
                item.Menu = c.MenuItems;
                ++item.MenuRevision;
                SendSignal(item, "/MenuBar", "com.canonical.dbusmenu", "LayoutUpdated");
            }
            A.dbus_connection_read_write(item.Conn, 0);
            while (A.dbus_connection_dispatch(item.Conn) == DBUS_DISPATCH_DATA_REMAINS) {}
            A.dbus_connection_flush(item.Conn);
            c.Registered = item.Registered;
        }

        // ---------------------------------------------------------------- test istemcisi
        struct ProbeClient
        {
            DBusConnection* Conn = nullptr;
            std::map<dbus_uint32_t, std::string> Pending;   // seri -> istek
            std::map<std::string, std::string> Values;
        };
        ProbeClient& Probe()
        {
            static ProbeClient p;
            return p;
        }

        void ParseItemProperties(DBusMessageIter* it, std::map<std::string, std::string>& values)
        {
            auto& A = Api();
            if (A.dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY)
                return;
            DBusMessageIter dict;
            A.dbus_message_iter_recurse(it, &dict);
            while (A.dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
            {
                DBusMessageIter entry, variant;
                A.dbus_message_iter_recurse(&dict, &entry);
                const std::string key = ReadString(&entry);
                A.dbus_message_iter_next(&entry);
                A.dbus_message_iter_recurse(&entry, &variant);
                const int type = A.dbus_message_iter_get_arg_type(&variant);
                if (type == DBUS_TYPE_STRING)
                    values[key] = ReadString(&variant);
                else if (type == DBUS_TYPE_OBJECT_PATH)
                {
                    const char* p = nullptr;
                    A.dbus_message_iter_get_basic(&variant, &p);
                    values[key] = p ? p : "";
                }
                else if (key == "ToolTip" && type == DBUS_TYPE_STRUCT)
                {
                    DBusMessageIter st;
                    A.dbus_message_iter_recurse(&variant, &st);
                    A.dbus_message_iter_next(&st);
                    A.dbus_message_iter_next(&st);
                    values["ToolTipTitle"] = ReadString(&st);
                }
                else if (key == "IconPixmap" && type == DBUS_TYPE_ARRAY)
                {
                    DBusMessageIter arr, st;
                    A.dbus_message_iter_recurse(&variant, &arr);
                    if (A.dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT)
                    {
                        A.dbus_message_iter_recurse(&arr, &st);
                        values["IconWidth"] = std::to_string(ReadInt(&st));
                    }
                }
                A.dbus_message_iter_next(&dict);
            }
        }

        void PumpProbe()
        {
            auto& A = Api();
            auto& p = Probe();
            if (!p.Conn)
                return;
            A.dbus_connection_read_write(p.Conn, 0);
            while (DBusMessage* msg = A.dbus_connection_pop_message(p.Conn))
            {
                const int type = A.dbus_message_get_type(msg);
                auto it = p.Pending.find(A.dbus_message_get_reply_serial(msg));
                if (it != p.Pending.end() && type == DBUS_MESSAGE_TYPE_METHOD_RETURN)
                {
                    DBusMessageIter args;
                    if (A.dbus_message_iter_init(msg, &args))
                    {
                        if (it->second == "getall")
                            ParseItemProperties(&args, p.Values);
                        else if (it->second == "layout")
                        {
                            A.dbus_message_iter_next(&args);   // sürüm
                            DBusMessageIter st, arr;
                            A.dbus_message_iter_recurse(&args, &st);
                            A.dbus_message_iter_next(&st);
                            A.dbus_message_iter_next(&st);
                            A.dbus_message_iter_recurse(&st, &arr);
                            int count = 0;
                            while (A.dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_VARIANT)
                            {
                                ++count;
                                A.dbus_message_iter_next(&arr);
                            }
                            p.Values["MenuItems"] = std::to_string(count);
                        }
                    }
                }
                if (it != p.Pending.end())
                    p.Pending.erase(it);
                A.dbus_message_unref(msg);
            }
        }
    }
#endif

#if defined(CQ_STATUS_WIN32)
    // ================================================================ Windows: Shell_NotifyIcon
    namespace
    {
        constexpr UINT kCallbackMessage = WM_APP + 0x51;
        constexpr UINT kMenuBase = 0x7100;

        struct WinItem
        {
            HWND Window = nullptr;
            HICON Icon = nullptr;
            NOTIFYICONDATAW Data{};
            std::string Tooltip, IconPath;
            bool Visible = true, Added = false;
            std::vector<std::string> Menu;
            std::vector<PendingEvent> Events;
        };

        std::wstring Wide(const std::string& s)
        {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            std::wstring w((size_t)n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
            return w;
        }

        HICON MakeIcon(const IconImage& img)
        {
            BITMAPV5HEADER bi{};
            bi.bV5Size = sizeof(bi);
            bi.bV5Width = img.Width;
            bi.bV5Height = -img.Height;
            bi.bV5Planes = 1;
            bi.bV5BitCount = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask = 0x00FF0000;
            bi.bV5GreenMask = 0x0000FF00;
            bi.bV5BlueMask = 0x000000FF;
            bi.bV5AlphaMask = 0xFF000000;
            void* bits = nullptr;
            HDC dc = GetDC(nullptr);
            HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, dc);
            if (!color)
                return nullptr;
            unsigned char* dst = static_cast<unsigned char*>(bits);
            for (size_t i = 0; i + 3 < img.Rgba.size(); i += 4)
            {
                dst[i] = img.Rgba[i + 2];
                dst[i + 1] = img.Rgba[i + 1];
                dst[i + 2] = img.Rgba[i];
                dst[i + 3] = img.Rgba[i + 3];
            }
            HBITMAP mask = CreateBitmap(img.Width, img.Height, 1, 1, nullptr);
            ICONINFO ii{};
            ii.fIcon = TRUE;
            ii.hbmMask = mask;
            ii.hbmColor = color;
            HICON icon = CreateIconIndirect(&ii);
            DeleteObject(color);
            DeleteObject(mask);
            return icon;
        }

        LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* item = reinterpret_cast<WinItem*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (item && msg == kCallbackMessage)
            {
                POINT pt{};
                GetCursorPos(&pt);
                const UINT event = LOWORD(lp);
                PendingEvent e;
                e.X = pt.x;
                e.Y = pt.y;
                if (event == WM_LBUTTONUP) e.Button = 1;
                else if (event == WM_MBUTTONUP) e.Button = 3;
                else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
                {
                    e.Button = 2;
                    if (!item->Menu.empty())
                    {
                        HMENU menu = CreatePopupMenu();
                        for (size_t i = 0; i < item->Menu.size(); ++i)
                        {
                            if (item->Menu[i] == "-") AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                            else AppendMenuW(menu, MF_STRING, kMenuBase + (UINT)i, Wide(item->Menu[i]).c_str());
                        }
                        SetForegroundWindow(hwnd);
                        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                        DestroyMenu(menu);
                        if (cmd >= kMenuBase && cmd < kMenuBase + item->Menu.size())
                        {
                            PendingEvent m;
                            m.Kind = 1;
                            m.Item = (int)(cmd - kMenuBase);
                            item->Events.push_back(m);
                        }
                    }
                }
                if (e.Button)
                    item->Events.push_back(e);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        void Apply(WinItem& item)
        {
            item.Data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            const std::wstring tip = Wide(item.Tooltip);
            wcsncpy_s(item.Data.szTip, tip.c_str(), _TRUNCATE);
            item.Data.hIcon = item.Icon;
            if (item.Visible && !item.Added)
                item.Added = Shell_NotifyIconW(NIM_ADD, &item.Data) != FALSE;
            else if (item.Visible && item.Added)
                Shell_NotifyIconW(NIM_MODIFY, &item.Data);
            else if (!item.Visible && item.Added)
            {
                Shell_NotifyIconW(NIM_DELETE, &item.Data);
                item.Added = false;
            }
        }

        WinItem* CreateItem(const StatusIndicatorComponent& c)
        {
            static bool registered = false;
            HINSTANCE instance = GetModuleHandleW(nullptr);
            if (!registered)
            {
                WNDCLASSW wc{};
                wc.lpfnWndProc = &WndProc;
                wc.hInstance = instance;
                wc.lpszClassName = L"ConquerorStatusIndicator";
                RegisterClassW(&wc);
                registered = true;
            }
            auto* item = new WinItem();
            item->Window = CreateWindowExW(0, L"ConquerorStatusIndicator", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
            SetWindowLongPtrW(item->Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(item));
            static UINT nextId = 1;
            item->Data.cbSize = sizeof(item->Data);
            item->Data.hWnd = item->Window;
            item->Data.uID = nextId++;
            item->Data.uCallbackMessage = kCallbackMessage;
            item->Tooltip = c.Tooltip;
            item->Visible = c.Visible;
            item->Menu = c.MenuItems;
            item->IconPath = c.Icon;
            item->Icon = MakeIcon(LoadIcon(c.Icon));
            Apply(*item);
            return item;
        }

        void DestroyItem(WinItem* item)
        {
            if (!item)
                return;
            if (item->Added)
                Shell_NotifyIconW(NIM_DELETE, &item->Data);
            if (item->Icon) DestroyIcon(item->Icon);
            if (item->Window) DestroyWindow(item->Window);
            delete item;
        }

        void Pump(WinItem& item, StatusIndicatorComponent& c)
        {
            bool changed = false;
            if (c.Tooltip != item.Tooltip) { item.Tooltip = c.Tooltip; changed = true; }
            if (c.Visible != item.Visible) { item.Visible = c.Visible; changed = true; }
            if (c.Icon != item.IconPath)
            {
                item.IconPath = c.Icon;
                if (item.Icon) DestroyIcon(item.Icon);
                item.Icon = MakeIcon(LoadIcon(c.Icon));
                changed = true;
            }
            item.Menu = c.MenuItems;
            if (changed)
                Apply(item);
            c.Registered = item.Added;
            // İleti kuyruğu ana döngüde (glfwPollEvents) işlenir; yalnız bu pencerenin iletileri de burada
            MSG msg;
            while (PeekMessageW(&msg, item.Window, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
#endif

    namespace
    {
#if defined(CQ_STATUS_SNI)
        using NativeItem = SniItem;
#elif defined(CQ_STATUS_WIN32)
        using NativeItem = WinItem;
#endif
        std::vector<Scene*>& Scenes()
        {
            static std::vector<Scene*> scenes;
            return scenes;
        }
    }

    namespace StatusIndicators
    {
        bool Supported()
        {
#if defined(CQ_STATUS_SNI)
            return Api().Ok;
#elif defined(CQ_STATUS_WIN32)
            return true;
#else
            return false;
#endif
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<StatusIndicatorComponent>();
            if (view.begin() == view.end())
                return;
            if (std::find(Scenes().begin(), Scenes().end(), &scene) == Scenes().end())
                Scenes().push_back(&scene);
#if defined(CQ_STATUS_SNI) || defined(CQ_STATUS_WIN32)
            std::vector<std::pair<uint32_t, PendingEvent>> events;
            for (auto e : view)
            {
                auto& c = view.get<StatusIndicatorComponent>(e);
                if (!scene.IsEntityActive(e))
                {
                    DestroyItem(static_cast<NativeItem*>(c.Native));
                    c.Native = nullptr;
                    c.Registered = false;
                    continue;
                }
                if (!c.Native)
                    c.Native = CreateItem(c);
                auto* item = static_cast<NativeItem*>(c.Native);
                if (!item)
                    continue;
                Pump(*item, c);
                for (const auto& ev : item->Events)
                    events.push_back({ (uint32_t)e, ev });
                item->Events.clear();
            }
            // Bildirimler en sonda (betik nesneyi silebilir)
            for (const auto& [id, ev] : events)
            {
                if (!scene.m_Registry.valid((entt::entity)id))
                    continue;
                auto& c = scene.m_Registry.get<StatusIndicatorComponent>((entt::entity)id);
                if (ev.Kind == 0)
                {
                    ++c.Presses;
                    c.LastButton = ev.Button;
                    c.LastPosition = glm::vec2((float)ev.X, (float)ev.Y);
                    CQS::Value args[3] = { CQS::Value::MakeInt(ev.Button), CQS::Value::MakeFloat(ev.X), CQS::Value::MakeFloat(ev.Y) };
                    Signals::Emit(scene, id, "pressed", args, 3);
                    if (scene.m_Registry.valid((entt::entity)id))
                        scene.InvokeScriptMethod(id, "OnStatusIndicatorPressed", args, 3);
                }
                else
                {
                    c.LastMenuItem = ev.Item;
                    CQS::Value args[1] = { CQS::Value::MakeInt(ev.Item) };
                    Signals::Emit(scene, id, "menu_item_pressed", args, 1);
                    if (scene.m_Registry.valid((entt::entity)id))
                        scene.InvokeScriptMethod(id, "OnStatusMenuItemPressed", args, 1);
                }
            }
#else
            static bool warned = false;
            if (!warned)
                CQ_CORE_WARN("[Status Indicator] Tray icons are not supported on this platform yet");
            warned = true;
#endif
        }

        void ForgetScene(Scene* scene)
        {
            auto it = std::find(Scenes().begin(), Scenes().end(), scene);
            if (it == Scenes().end())
                return;
            Scenes().erase(it);
#if defined(CQ_STATUS_SNI) || defined(CQ_STATUS_WIN32)
            auto view = scene->m_Registry.view<StatusIndicatorComponent>();
            for (auto e : view)
            {
                auto& c = view.get<StatusIndicatorComponent>(e);
                DestroyItem(static_cast<NativeItem*>(c.Native));
                c.Native = nullptr;
                c.Registered = false;
            }
#endif
        }

        bool ProbeSend(Scene& scene, uint32_t entity, const std::string& request, int argument)
        {
#if defined(CQ_STATUS_SNI)
            auto& A = Api();
            const entt::entity e = (entt::entity)entity;
            auto* c = scene.m_Registry.valid(e) ? scene.m_Registry.try_get<StatusIndicatorComponent>(e) : nullptr;
            auto* item = c ? static_cast<SniItem*>(c->Native) : nullptr;
            if (!A.Ok || !item)
                return false;
            auto& p = Probe();
            if (!p.Conn)
            {
                DBusError err;
                A.dbus_error_init(&err);
                p.Conn = A.dbus_bus_get_private(DBUS_BUS_SESSION, &err);
                A.dbus_error_free(&err);
                if (!p.Conn)
                    return false;
                A.dbus_connection_set_exit_on_disconnect(p.Conn, FALSE);
            }
            const bool menu = request == "menu" || request == "layout";
            const char* path = menu ? "/MenuBar" : "/StatusNotifierItem";
            const char* iface = request == "getall" ? "org.freedesktop.DBus.Properties" : menu ? "com.canonical.dbusmenu" : "org.kde.StatusNotifierItem";
            const char* method = request == "getall" ? "GetAll" : request == "activate" ? "Activate" : request == "context" ? "ContextMenu"
                               : request == "secondary" ? "SecondaryActivate" : request == "menu" ? "Event" : "GetLayout";
            DBusMessage* m = A.dbus_message_new_method_call(item->Service.c_str(), path, iface, method);
            if (!m)
                return false;
            DBusMessageIter it;
            A.dbus_message_iter_init_append(m, &it);
            if (request == "getall")
                Str(&it, "org.kde.StatusNotifierItem");
            else if (request == "layout")
            {
                Int(&it, 0);
                Int(&it, -1);
                Container(&it, DBUS_TYPE_ARRAY, "s", [](DBusMessageIter*) {});
            }
            else if (request == "menu")
            {
                Int(&it, argument + 1);
                Str(&it, "clicked");
                Container(&it, DBUS_TYPE_VARIANT, "i", [](DBusMessageIter* v) { Int(v, 0); });
                UInt(&it, 0);
            }
            else
            {
                Int(&it, 10 + argument);
                Int(&it, 20 + argument);
            }
            dbus_uint32_t serial = 0;
            A.dbus_connection_send(p.Conn, m, &serial);
            A.dbus_message_unref(m);
            A.dbus_connection_flush(p.Conn);
            p.Pending[serial] = request;
            return true;
#else
            (void)scene; (void)entity; (void)request; (void)argument;
            return false;
#endif
        }

        bool ProbeValue(const std::string& key, std::string& out)
        {
#if defined(CQ_STATUS_SNI)
            PumpProbe();
            auto it = Probe().Values.find(key);
            if (it == Probe().Values.end())
                return false;
            out = it->second;
            return true;
#else
            (void)key; (void)out;
            return false;
#endif
        }
    }

    template<> void Scene::OnComponentAdded<StatusIndicatorComponent>(Entity, StatusIndicatorComponent&) {}

    void RegisterStatusIndicatorComponents()
    {
        using C = StatusIndicatorComponent;
        ComponentDef<C>("Status Indicator", "StatusIndicatorComponent", "UI", ICON_FA_BELL)
            .Describe("Godot StatusIndicator: an icon in the system tray (notification area) while the game runs, with a tooltip and a "
                      "menu. A click emits pressed (button 1 left, 2 right, 3 middle, x, y) and calls OnStatusIndicatorPressed; a menu "
                      "item emits menu_item_pressed (index) and calls OnStatusMenuItemPressed. Linux / BSD: StatusNotifierItem (KDE, "
                      "XFCE, LXQt; GNOME with the AppIndicator extension); Windows: notification area. Unity and Unreal have no equivalent.")
            .Field("Icon", "Icon", &C::Icon, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga;.bmp")
            .Field("Tooltip", "Tooltip", &C::Tooltip)
            .Field("Visible", "Visible", &C::Visible)
            .Field("Registered", "In Tray", &C::Registered).ReadOnly()
            .Field("Presses", "Presses", &C::Presses).ReadOnly()
            .Field("LastButton", "Last Button", &C::LastButton).ReadOnly()
            .Field("LastMenuItem", "Last Menu Item", &C::LastMenuItem).ReadOnly()
            .CustomData(
                [](YAML::Emitter& out, C& c) {
                    out << YAML::Key << "MenuItems" << YAML::Value << YAML::BeginSeq;
                    for (const auto& m : c.MenuItems) out << m;
                    out << YAML::EndSeq;
                },
                [](const YAML::Node& n, C& c) {
                    c.MenuItems.clear();
                    if (auto list = n["MenuItems"]; list && list.IsSequence())
                        for (const auto& m : list) c.MenuItems.push_back(m.as<std::string>());
                });
    }
}
