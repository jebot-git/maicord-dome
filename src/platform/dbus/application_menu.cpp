// Hierarchical com.canonical.dbusmenu for DomeD's universal menu, separate from the tray menu.
#include "maicord/platform/dbus/application_menu.hpp"
#include "maicord/platform/dbus/bus.hpp"
#include "maicord/platform/dbus/session.hpp"
#include "maicord/core/memory.hpp"
#include "maicord/core/slice.hpp"
namespace maicord::dbus {namespace {
constexpr const char* kMenuPath="/ApplicationMenu";
constexpr const char* kMenuInterface="com.canonical.dbusmenu";
constexpr u32 kMaxItems=48;
struct Item {u32 parent,command;bool enabled,checkable,checked,separator;char label[96],modifier[24],key[24];};
struct State {bool started,hook;ApplicationMenuHandler handler;ApplicationMenuBind bind;Item items[kMaxItems];u32 item_count,revision;} g;
const char kMenuIntrospection[] =
    "<interface name=\"com.canonical.dbusmenu\">"
    "<property name=\"Version\" type=\"u\" access=\"read\"/>"
    "<property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
    "<property name=\"Status\" type=\"s\" access=\"read\"/>"
    "<property name=\"IconThemePath\" type=\"as\" access=\"read\"/>"
    "<method name=\"GetLayout\"><arg type=\"i\" name=\"parentId\" direction=\"in\"/><arg type=\"i\" name=\"recursionDepth\" direction=\"in\"/>"
    "<arg type=\"as\" name=\"propertyNames\" direction=\"in\"/><arg type=\"u\" name=\"revision\" direction=\"out\"/>"
    "<arg type=\"(ia{sv}av)\" name=\"layout\" direction=\"out\"/></method>"
    "<method name=\"GetGroupProperties\"><arg type=\"ai\" name=\"ids\" direction=\"in\"/><arg type=\"as\" name=\"propertyNames\" direction=\"in\"/>"
    "<arg type=\"a(ia{sv})\" name=\"properties\" direction=\"out\"/></method>"
    "<method name=\"GetProperty\"><arg type=\"i\" name=\"id\" direction=\"in\"/><arg type=\"s\" name=\"name\" direction=\"in\"/>"
    "<arg type=\"v\" name=\"value\" direction=\"out\"/></method>"
    "<method name=\"Event\"><arg type=\"i\" name=\"id\" direction=\"in\"/><arg type=\"s\" name=\"eventId\" direction=\"in\"/>"
    "<arg type=\"v\" name=\"data\" direction=\"in\"/><arg type=\"u\" name=\"timestamp\" direction=\"in\"/></method>"
    "<method name=\"EventGroup\"><arg type=\"a(isvu)\" name=\"events\" direction=\"in\"/><arg type=\"ai\" name=\"idErrors\" direction=\"out\"/></method>"
    "<method name=\"AboutToShow\"><arg type=\"i\" name=\"id\" direction=\"in\"/><arg type=\"b\" name=\"needUpdate\" direction=\"out\"/></method>"
    "<method name=\"AboutToShowGroup\"><arg type=\"ai\" name=\"ids\" direction=\"in\"/><arg type=\"ai\" name=\"updatesNeeded\" direction=\"out\"/>"
    "<arg type=\"ai\" name=\"idErrors\" direction=\"out\"/></method>"
    "<signal name=\"ItemsPropertiesUpdated\"><arg type=\"a(ia{sv})\" name=\"updatedProps\"/><arg type=\"a(ias)\" name=\"removedProps\"/></signal>"
    "<signal name=\"LayoutUpdated\"><arg type=\"u\" name=\"revision\"/><arg type=\"i\" name=\"parent\"/></signal>"
    "<signal name=\"ItemActivationRequested\"><arg type=\"i\" name=\"id\"/><arg type=\"u\" name=\"timestamp\"/></signal>"
    "</interface>";
void variant_text(Builder* b, const char* signature, const char* text) noexcept {
    builder_open_variant(b, signature);
    builder_string(b, text);
    builder_close_variant(b);
}

const ExportedProperty kMenuProperties[] = {
    {kMenuInterface, "Version"},
    {kMenuInterface, "TextDirection"},
    {kMenuInterface, "Status"},
    {kMenuInterface, "IconThemePath"},
};

bool write_menu_property(void*, const char*, const char* name, Builder* b) noexcept {
    if (c_string_equal(name, "Version")) {
        builder_open_variant(b, "u");
        builder_u32(b, 3);
        builder_close_variant(b);
    } else if (c_string_equal(name, "TextDirection")) {
        variant_text(b, "s", "ltr");
    } else if (c_string_equal(name, "Status")) {
        variant_text(b, "s", "normal");
    } else if (c_string_equal(name, "IconThemePath")) {
        builder_open_variant(b, "as");
        builder_open_array(b, "s");
        builder_close_array(b);
        builder_close_variant(b);
    } else {
        return false;
    }
    return true;
}

ExportedObject menu_object() noexcept {
    return ExportedObject{kMenuProperties, static_cast<u32>(sizeof(kMenuProperties) / sizeof(kMenuProperties[0])),
                          write_menu_property, nullptr, kMenuIntrospection};
}

const Item* item_by_id(i32 id) noexcept {
    return id >= 1 && static_cast<u32>(id) <= g.item_count ? &g.items[id - 1] : nullptr;
}

// a{sv} of one item (id 0 is the root menu).
void write_item_properties(Builder* b, i32 id) noexcept {
    builder_open_array(b, "{sv}");
    if (id == 0) {
        builder_open_dict_entry(b, "children-display", "s");
        builder_string(b, "submenu");
        builder_close_dict_entry(b);
    } else if (const Item* item = item_by_id(id)) {
        if (item->separator) {
            builder_dict_string(b, "type", "separator");
        } else {
            builder_dict_string(b, "label", item->label);
            builder_dict_bool(b, "enabled", item->enabled);
            builder_dict_bool(b, "visible", true);
            if(!item->command)builder_dict_string(b,"children-display","submenu");
            if(item->key[0]) {
                builder_open_dict_entry(b,"shortcut","aas");builder_open_array(b,"as");builder_open_array(b,"s");
                if(item->modifier[0])builder_string(b,item->modifier);
                builder_string(b,item->key);builder_close_array(b);builder_close_array(b);builder_close_dict_entry(b);
            }
            if (item->checkable) {
                builder_dict_string(b, "toggle-type", "checkmark");
                builder_open_dict_entry(b, "toggle-state", "i");
                builder_i32(b, item->checked ? 1 : 0);
                builder_close_dict_entry(b);
            }
        }
    }
    builder_close_array(b);
}

// (ia{sv}av): the item and, within the depth, its children (only the root has any).
void write_layout(Builder* b, i32 id, i32 depth) noexcept {
    builder_open_struct(b, "(ia{sv}av)");
    builder_i32(b, id);
    write_item_properties(b, id);
    builder_open_array(b, "v");
    if (depth != 0) {
        for (u32 i = 0; i < g.item_count; ++i) {
            if(g.items[i].parent!=static_cast<u32>(id))continue;
            builder_open_variant(b, "(ia{sv}av)");
            write_layout(b, static_cast<i32>(i + 1), depth > 0 ? depth - 1 : -1);
            builder_close_variant(b);
        }
    }
    builder_close_array(b);
    builder_close_struct(b);
}

void menu_clicked(i32 id) noexcept {
    const Item* item = item_by_id(id);
    if (item && item->enabled && !item->separator && item->command && g.handler)
        g.handler(item->command);
}

bool menu_call(void*, Connection* c, const Message& call) noexcept {
    if (object_handle_standard(c, call, menu_object()))
        return true;
    if (!call.interface || !c_string_equal(call.interface, kMenuInterface))
        return false;
    Reader r;
    message_reader(call, &r);
    Builder body;
    builder_init(&body);
    bool ok = true;
    if (c_string_equal(call.member, "GetLayout")) {
        i32 parent = 0;
        i32 depth = 0;
        ok = reader_i32(&r, &parent) && reader_i32(&r, &depth);
        if (!ok || (parent != 0 && !item_by_id(parent))) {
            builder_release(&body);
            connection_reply_error(c, call, "org.freedesktop.DBus.Error.InvalidArgs", "no such menu item");
            return true;
        }
        builder_u32(&body, g.revision);
        write_layout(&body, parent, depth);
    } else if (c_string_equal(call.member, "GetGroupProperties")) {
        bool all = true;
        ok = reader_enter_array(&r);
        u32 ids[kMaxItems + 1];
        u32 count = 0;
        while (ok && reader_more(r)) {
            i32 id = 0;
            ok = reader_i32(&r, &id);
            if (ok && count < kMaxItems + 1)
                ids[count++] = static_cast<u32>(id);
            all = false;
        }
        ok = ok && reader_leave(&r);
        if (all) {
            for (u32 i = 0; i < g.item_count; ++i)
                ids[count++] = i + 1;
        }
        builder_open_array(&body, "(ia{sv})");
        for (u32 i = 0; ok && i < count; ++i) {
            if (ids[i] != 0 && !item_by_id(static_cast<i32>(ids[i])))
                continue;
            builder_open_struct(&body, "(ia{sv})");
            builder_i32(&body, static_cast<i32>(ids[i]));
            write_item_properties(&body, static_cast<i32>(ids[i]));
            builder_close_struct(&body);
        }
        builder_close_array(&body);
    } else if (c_string_equal(call.member, "GetProperty")) {
        // The one property asked for, taken out of the item's full set.
        i32 id = 0;
        const char* name = nullptr;
        ok = reader_i32(&r, &id) && reader_string(&r, &name, nullptr);
        Builder all;
        builder_init(&all);
        write_item_properties(&all, id);
        Reader a;
        reader_init(&a, all.data, all.size, "a{sv}", nullptr, 0);
        bool found = false;
        if (ok && reader_enter_array(&a)) {
            while (!found && reader_more(a)) {
                const char* key = nullptr;
                ok = reader_enter_struct(&a) && reader_string(&a, &key, nullptr);
                if (ok && c_string_equal(key, name)) {
                    // Copy the variant by re-reading: only strings, booleans and integers occur.
                    const char* signature = nullptr;
                    ok = reader_enter_variant(&a, &signature);
                    if (ok) {
                        builder_open_variant(&body, signature);
                        if (signature[0] == 's') {
                            const char* text = nullptr;
                            ok = reader_string(&a, &text, nullptr);
                            builder_string(&body, text ? text : "");
                        } else if (signature[0] == 'b') {
                            bool value = false;
                            ok = reader_bool(&a, &value);
                            builder_bool(&body, value);
                        } else if(signature[0]=='a') {
                            builder_open_array(&body,"as");
                            ok=reader_enter_array(&a);
                            while(ok && reader_more(a)) {
                                ok=reader_enter_array(&a);builder_open_array(&body,"s");
                                while(ok && reader_more(a)) {
                                    const char* value=nullptr;ok=reader_string(&a,&value,nullptr);
                                    if(ok)builder_string(&body,value);
                                }
                                ok=ok && reader_leave(&a);builder_close_array(&body);
                            }
                            ok=ok && reader_leave(&a);builder_close_array(&body);
                        } else {
                            i32 value = 0;
                            ok = reader_i32(&a, &value);
                            builder_i32(&body, value);
                        }
                        builder_close_variant(&body);
                        found = ok;
                    }
                    break;
                }
                ok = ok && reader_leave(&a);
            }
        }
        builder_release(&all);
        if (!found) {
            builder_release(&body);
            connection_reply_error(c, call, "org.freedesktop.DBus.Error.InvalidArgs", "no such property");
            return true;
        }
    } else if (c_string_equal(call.member, "Event")) {
        i32 id = 0;
        const char* event = nullptr;
        ok = reader_i32(&r, &id) && reader_string(&r, &event, nullptr);
        if (ok && c_string_equal(event, "clicked"))
            menu_clicked(id);
    } else if (c_string_equal(call.member, "EventGroup")) {
        ok = reader_enter_array(&r);
        while (ok && reader_more(r)) {
            i32 id = 0;
            const char* event = nullptr;
            ok = reader_enter_struct(&r) && reader_i32(&r, &id) && reader_string(&r, &event, nullptr) && reader_leave(&r);
            if (ok && c_string_equal(event, "clicked"))
                menu_clicked(id);
        }
        builder_open_array(&body, "i");
        builder_close_array(&body);
    } else if (c_string_equal(call.member, "AboutToShow")) {
        builder_bool(&body, false);
    } else if (c_string_equal(call.member, "AboutToShowGroup")) {
        builder_open_array(&body, "i");
        builder_close_array(&body);
        builder_open_array(&body, "i");
        builder_close_array(&body);
    } else {
        builder_release(&body);
        return false;
    }
    if (!ok || !builder_ok(body)) {
        builder_release(&body);
        connection_reply_error(c, call, "org.freedesktop.DBus.Error.InvalidArgs", "malformed arguments");
        return true;
    }
    connection_reply(c, call, &body);
    builder_release(&body);
    return true;
}


void on_session(Connection* c) noexcept {
    if(g.started && connection_export_object(c,kMenuPath,menu_call,nullptr) && g.bind)
        g.bind(connection_unique_name(c),kMenuPath);
}
template<usize N> void text(char (&out)[N],const char* value) noexcept {
    u32 i=0;if(value)while(value[i] && i+1<N){out[i]=value[i];++i;}out[i]=0;
}
} // namespace
bool application_menu_start(ApplicationMenuHandler handler,ApplicationMenuBind bind) noexcept {
    g.started=true;g.handler=handler;g.bind=bind;
    if(!g.hook){g.hook=session_bus_add_hook(on_session);if(!g.hook)return false;}
    if(!session_bus_open())return false;
    if(auto* c=session_bus())on_session(c);
    return true;
}
void application_menu_update(const ApplicationMenuEntry* entries,u32 count) noexcept {
    if(!entries || count>kMaxItems)return;
    Item next[kMaxItems]{};
    for(u32 i=0;i<count;++i) {
        if(entries[i].parent>i)return; // bounded acyclic hierarchy
        auto& item=next[i];const auto& entry=entries[i];
        item.parent=entry.parent;item.command=entry.command;item.enabled=entry.enabled;
        item.checkable=entry.checkable;item.checked=entry.checked;
        text(item.label,entry.label);text(item.modifier,entry.modifier);text(item.key,entry.key);
    }
    if(g.item_count==count && bytes_equal(g.items,next,count*sizeof(Item)))return;
    copy_bytes(g.items,next,count*sizeof(Item));g.item_count=count;++g.revision;
    if(auto* c=session_bus()) {
        Builder body;builder_init(&body);builder_u32(&body,g.revision);builder_i32(&body,0);
        connection_emit(c,kMenuPath,kMenuInterface,"LayoutUpdated",&body);builder_release(&body);
    }
}
void application_menu_stop() noexcept {
    if(auto* c=session_bus())connection_unexport_object(c,kMenuPath);
    g.started=false;
}
} // namespace maicord::dbus
