//! Conqueror Engine Zig script API. The engine copies this file, the generated nativebridge.zig and the project's
//! .zig scripts into one directory and builds a dynamic library (zig build-lib -dynamic -lc). The boundary with the
//! engine is the same C ABI that C, C++ and Rust scripts use. Fields and methods are found with comptime reflection:
//!
//!     const cq = @import("conqueror.zig");
//!     const nb = @import("nativebridge.zig");
//!
//!     pub const Spinner = struct {
//!         speed: f32 = 90.0,                     // public fields with defaults appear in the Inspector
//!         ticks: i32 = 0,
//!         label: cq.String = cq.String.init("spin"),
//!         entity: u32 = 0,                       // optional: receives the entity id
//!
//!         pub fn update(self: *Spinner, dt: f32) void {
//!             const r = nb.GetRotation(.{});
//!             _ = nb.SetRotation(.{ r.x(), r.y() + self.speed * dt, r.z() });
//!         }
//!         pub fn PlayBtn(self: *Spinner) void { self.ticks = 0; }   // UI events / other engine events by name
//!     };
const std = @import("std");

// ── C ABI (identical to the engine's CQNativeValue, 64 bytes) ──
pub const RawValue = extern struct {
    ty: i32 = 0,
    dim: i32 = 0,
    i: i64 = 0,
    x: f64 = 0,
    y: f64 = 0,
    z: f64 = 0,
    w: f64 = 0,
    s: ?[*]const u8 = null,
    len: i32 = 0,
    pad: i32 = 0,
};

pub const HostApi = extern struct {
    version: c_int,
    find_native: *const fn ([*:0]const u8) callconv(.c) c_int,
    call_native: *const fn (c_int, c_int, ?[*]const RawValue, *RawValue) callconv(.c) c_int,
    log: *const fn (c_int, [*:0]const u8) callconv(.c) void,
};

var host: ?*const HostApi = null;
const allocator = std.heap.c_allocator;

pub fn init(api: *const HostApi) void {
    host = api;
}

// ── Values ──
pub const Value = extern struct {
    raw: RawValue = .{},

    pub fn x(v: Value) f32 {
        return if (v.raw.ty == 5) @floatCast(v.raw.x) else 0;
    }
    pub fn y(v: Value) f32 {
        return if (v.raw.ty == 5) @floatCast(v.raw.y) else 0;
    }
    pub fn z(v: Value) f32 {
        return if (v.raw.ty == 5) @floatCast(v.raw.z) else 0;
    }
    pub fn w(v: Value) f32 {
        return if (v.raw.ty == 5) @floatCast(v.raw.w) else 0;
    }
    pub fn isNull(v: Value) bool {
        return v.raw.ty == 0;
    }
    pub fn asFloat(v: Value) f64 {
        return switch (v.raw.ty) {
            1, 2 => @floatFromInt(v.raw.i),
            3, 5 => v.raw.x,
            4 => std.fmt.parseFloat(f64, v.asString()) catch 0,
            else => 0,
        };
    }
    pub fn asInt(v: Value) i64 {
        return switch (v.raw.ty) {
            1, 2 => v.raw.i,
            else => std.math.lossyCast(i64, v.asFloat()),
        };
    }
    pub fn asBool(v: Value) bool {
        return switch (v.raw.ty) {
            0 => false,
            1, 2 => v.raw.i != 0,
            3 => v.raw.x != 0,
            4 => v.raw.len > 0,
            else => true,
        };
    }
    /// Text of a string value; engine results stay valid until the next engine call
    pub fn asString(v: Value) []const u8 {
        if ((v.raw.ty != 4 and v.raw.ty != 6) or v.raw.s == null) return "";
        return v.raw.s.?[0..@intCast(@max(v.raw.len, 0))];
    }
};

pub fn str(s: []const u8) Value {
    return .{ .raw = .{ .ty = 4, .s = s.ptr, .len = @intCast(s.len) } };
}
pub fn vec3(vx: f64, vy: f64, vz: f64) Value {
    return .{ .raw = .{ .ty = 5, .dim = 3, .x = vx, .y = vy, .z = vz } };
}
pub fn vec2(vx: f64, vy: f64) Value {
    return .{ .raw = .{ .ty = 5, .dim = 2, .x = vx, .y = vy } };
}

/// Any Zig value -> engine value (numbers, bool, strings, Value, null)
pub fn from(v: anytype) Value {
    const T = @TypeOf(v);
    if (T == Value) return v;
    if (T == String) return str(v.slice());
    switch (@typeInfo(T)) {
        .bool => return .{ .raw = .{ .ty = 1, .i = @intFromBool(v) } },
        .int, .comptime_int => return .{ .raw = .{ .ty = 2, .i = @intCast(v) } },
        .float, .comptime_float => return .{ .raw = .{ .ty = 3, .x = @floatCast(v) } },
        .null => return .{},
        .optional => return if (v) |inner| from(inner) else .{},
        .pointer => |p| {
            if (p.size == .slice and p.child == u8) return str(v);
            if (p.size == .one) {
                const child = @typeInfo(p.child);
                if (child == .array and child.array.child == u8) return str(v);
            }
            @compileError("cannot pass " ++ @typeName(T) ++ " to the engine");
        },
        else => @compileError("cannot pass " ++ @typeName(T) ++ " to the engine"),
    }
}

/// Fixed-capacity text field (shown as a string in the Inspector)
pub const String = struct {
    buf: [128]u8 = @splat(0),
    len: usize = 0,

    pub fn init(comptime text: []const u8) String {
        var s: String = .{};
        @memcpy(s.buf[0..text.len], text);
        s.len = text.len;
        return s;
    }
    pub fn set(s: *String, text: []const u8) void {
        const n = @min(text.len, s.buf.len);
        @memcpy(s.buf[0..n], text[0..n]);
        s.len = n;
    }
    pub fn slice(s: *const String) []const u8 {
        return s.buf[0..s.len];
    }
};

// ── Engine calls ──
pub fn findNative(name: [*:0]const u8) i32 {
    const h = host orelse return -1;
    return h.find_native(name);
}

/// Call an engine function by index with a tuple of Zig values: .{ x, y, z }
pub fn callIndex(index: i32, args: anytype) Value {
    const h = host orelse return .{};
    if (index < 0) return .{};
    const Args = @TypeOf(args);
    const count = comptime tupleLen(Args);
    var raw: [count + 1]RawValue = undefined;
    inline for (0..count) |i| raw[i] = from(@field(args, fieldName(Args, i))).raw;
    var result: RawValue = .{};
    if (h.call_native(index, @intCast(count), &raw, &result) == 0) return .{};
    return .{ .raw = result };
}

/// Used by the generated nativebridge.zig (index cached per function)
pub fn callCached(index: *i32, comptime name: [:0]const u8, args: anytype) Value {
    if (index.* == -2) index.* = findNative(name.ptr);
    return callIndex(index.*, args);
}

/// Call any engine function by name: cq.call("GetPosition", .{})
pub fn call(comptime name: [:0]const u8, args: anytype) Value {
    return callIndex(findNative(name.ptr), args);
}

fn logLevel(level: c_int, comptime fmt: []const u8, args: anytype) void {
    const h = host orelse return;
    var buf: [2048]u8 = undefined;
    const text = std.fmt.bufPrint(buf[0 .. buf.len - 1], fmt, args) catch buf[0 .. buf.len - 1];
    buf[text.len] = 0;
    h.log(level, @ptrCast(&buf));
}
pub fn log(comptime fmt: []const u8, args: anytype) void {
    logLevel(0, fmt, args);
}
pub fn logWarning(comptime fmt: []const u8, args: anytype) void {
    logLevel(1, fmt, args);
}
pub fn logError(comptime fmt: []const u8, args: anytype) void {
    logLevel(2, fmt, args);
}

// ── Reflection helpers (Zig 0.14/0.15: fields/decls/params; 0.16+: field_names/field_types/decl_names/param_types) ──
const StructInfo = @TypeOf(@typeInfo(struct {}).@"struct");
const new_struct_info = @hasField(StructInfo, "field_names");
const FnInfo = @TypeOf(@typeInfo(fn () void).@"fn");
const new_fn_info = @hasField(FnInfo, "param_types");

fn fieldCount(comptime T: type) usize {
    const info = @typeInfo(T).@"struct";
    if (comptime new_struct_info) return info.field_names.len else return info.fields.len;
}
fn fieldName(comptime T: type, comptime i: usize) [:0]const u8 {
    const info = @typeInfo(T).@"struct";
    if (comptime new_struct_info) return info.field_names[i] else return info.fields[i].name;
}
fn FieldType(comptime T: type, comptime i: usize) type {
    const info = @typeInfo(T).@"struct";
    if (comptime new_struct_info) return info.field_types[i] else return info.fields[i].type;
}
fn declCount(comptime T: type) usize {
    const info = @typeInfo(T).@"struct";
    if (comptime new_struct_info) return info.decl_names.len else return info.decls.len;
}
fn declName(comptime T: type, comptime i: usize) [:0]const u8 {
    const info = @typeInfo(T).@"struct";
    if (comptime new_struct_info) return info.decl_names[i] else return info.decls[i].name;
}
fn paramCount(comptime F: type) usize {
    const info = @typeInfo(F).@"fn";
    if (comptime new_fn_info) return info.param_types.len else return info.params.len;
}
fn ParamType(comptime F: type, comptime i: usize) ?type {
    const info = @typeInfo(F).@"fn";
    if (comptime new_fn_info) return info.param_types[i] else return info.params[i].type;
}
fn tupleLen(comptime T: type) usize {
    return fieldCount(T);
}

// ── Class registry (the generated root lists the script structs) ──
pub fn class(comptime T: type, comptime name: []const u8) type {
    return struct {
        pub const Type = T;
        pub const class_name = name;
    };
}

const Instance = struct {
    class_index: usize,
    ptr: *anyopaque,
};

var error_buf: [512:0]u8 = @splat(0);

fn setError(comptime fmt: []const u8, args: anytype) void {
    const text = std.fmt.bufPrint(error_buf[0 .. error_buf.len - 1], fmt, args) catch error_buf[0 .. error_buf.len - 1];
    error_buf[text.len] = 0;
}

pub fn lastError() [*:0]const u8 {
    return &error_buf;
}

// Growable text buffer for describe() / string fields
var text_buf: []u8 = &.{};
var text_len: usize = 0;

fn textAppend(s: []const u8) void {
    if (text_len + s.len + 1 > text_buf.len) {
        var cap: usize = if (text_buf.len == 0) 1024 else text_buf.len * 2;
        while (cap < text_len + s.len + 1) cap *= 2;
        text_buf = allocator.realloc(text_buf, cap) catch return;
    }
    @memcpy(text_buf[text_len..][0..s.len], s);
    text_len += s.len;
    text_buf[text_len] = 0;
}

fn textJsonString(s: []const u8) void {
    textAppend("\"");
    for (s) |c| {
        switch (c) {
            '"' => textAppend("\\\""),
            '\\' => textAppend("\\\\"),
            '\n' => textAppend("\\n"),
            else => {
                if (c < 0x20) {
                    var b: [8]u8 = undefined;
                    textAppend(std.fmt.bufPrint(&b, "\\u{x:0>4}", .{c}) catch "");
                } else textAppend(&[_]u8{c});
            },
        }
    }
    textAppend("\"");
}

fn isInspectorField(comptime name: []const u8) bool {
    return !std.mem.eql(u8, name, "entity") and name.len > 0 and name[0] != '_';
}

fn typeName(comptime T: type) ?[:0]const u8 {
    if (T == f32) return "float";
    if (T == f64) return "double";
    if (T == bool) return "bool";
    if (T == String) return "string";
    if (@typeInfo(T) == .int) return "int";
    return null;
}

fn toRaw(v: anytype) RawValue {
    const T = @TypeOf(v);
    if (T == String) return str(v.slice()).raw;
    if (T == f32 or T == f64) return .{ .ty = 3, .x = @floatCast(v) };
    if (T == bool) return .{ .ty = 1, .i = @intFromBool(v) };
    return .{ .ty = 2, .i = std.math.lossyCast(i64, v) };
}

fn assign(comptime T: type, dst: *T, v: Value) void {
    if (T == String) {
        dst.set(v.asString());
    } else if (T == f32 or T == f64) {
        dst.* = @floatCast(v.asFloat());
    } else if (T == bool) {
        dst.* = v.asBool();
    } else if (@typeInfo(T) == .int) {
        dst.* = std.math.lossyCast(T, v.asInt());
    }
}

pub fn describe(comptime classes: anytype) [*:0]const u8 {
    text_len = 0;
    textAppend("[");
    inline for (classes, 0..) |C, ci| {
        if (ci > 0) textAppend(",");
        textAppend("{\"name\":");
        textJsonString(C.class_name);
        textAppend(",\"fields\":[");
        const defaults: C.Type = .{};
        comptime var written = 0;
        inline for (0..comptime fieldCount(C.Type)) |fi| {
            const name = comptime fieldName(C.Type, fi);
            const FT = comptime FieldType(C.Type, fi);
            if (comptime (isInspectorField(name) and typeName(FT) != null)) {
                if (written > 0) textAppend(",");
                written += 1;
                textAppend("{\"name\":");
                textJsonString(name);
                textAppend(",\"type\":\"");
                textAppend(typeName(FT).?);
                textAppend("\",\"default\":");
                const d = @field(defaults, name);
                var b: [64]u8 = undefined;
                if (FT == String) {
                    textJsonString(d.slice());
                } else if (FT == bool) {
                    textAppend(if (d) "true" else "false");
                } else if (FT == f32 or FT == f64) {
                    textAppend(std.fmt.bufPrint(&b, "{d}", .{@as(f64, @floatCast(d))}) catch "0");
                } else {
                    textAppend(std.fmt.bufPrint(&b, "{d}", .{d}) catch "0");
                }
                textAppend(",\"attributes\":[]}");
            }
        }
        textAppend("]}");
    }
    textAppend("]");
    if (text_buf.len == 0) return "[]";
    return @ptrCast(text_buf.ptr);
}

pub fn create(comptime classes: anytype, name: [*:0]const u8, entity: u32) ?*anyopaque {
    const n = std.mem.span(name);
    inline for (classes, 0..) |C, ci| {
        if (std.mem.eql(u8, n, C.class_name)) {
            const obj = allocator.create(C.Type) catch {
                setError("out of memory", .{});
                return null;
            };
            obj.* = .{};
            if (@hasField(C.Type, "entity")) obj.entity = entity;
            const inst = allocator.create(Instance) catch {
                allocator.destroy(obj);
                setError("out of memory", .{});
                return null;
            };
            inst.* = .{ .class_index = ci, .ptr = obj };
            return inst;
        }
    }
    setError("Zig script class '{s}' not found (declare it as 'pub const {s} = struct {{ ... }};')", .{ n, n });
    return null;
}

pub fn destroy(comptime classes: anytype, handle: ?*anyopaque) void {
    const inst: *Instance = @ptrCast(@alignCast(handle orelse return));
    inline for (classes, 0..) |C, ci| {
        if (inst.class_index == ci) {
            const obj: *C.Type = @ptrCast(@alignCast(inst.ptr));
            allocator.destroy(obj);
        }
    }
    allocator.destroy(inst);
}

// Engine names are PascalCase ("Update", "PlayBtn"); Zig methods may be camelCase ("update", "playBtn")
fn nameMatches(comptime decl: []const u8, name: []const u8) bool {
    if (decl.len != name.len or decl.len == 0) return false;
    if (std.ascii.toLower(decl[0]) != std.ascii.toLower(name[0])) return false;
    return std.mem.eql(u8, decl[1..], name[1..]);
}

fn isMethod(comptime T: type, comptime decl: []const u8) bool {
    const D = @TypeOf(@field(T, decl));
    if (@typeInfo(D) != .@"fn") return false;
    if (paramCount(D) == 0) return false;
    const P = ParamType(D, 0) orelse return false;
    return P == *T or P == *const T or P == T;
}

fn argAs(comptime P: type, values: []const RawValue, index: usize) P {
    const v: Value = if (index < values.len) .{ .raw = values[index] } else .{};
    if (P == Value) return v;
    if (P == []const Value) return @as([*]const Value, @ptrCast(values.ptr))[0..values.len];
    if (P == []const u8) return v.asString();
    if (P == f32 or P == f64) return @floatCast(v.asFloat());
    if (P == bool) return v.asBool();
    if (@typeInfo(P) == .int) return std.math.lossyCast(P, v.asInt());
    @compileError("unsupported engine callback parameter type " ++ @typeName(P));
}

fn callMethod(comptime T: type, obj: *T, comptime decl: []const u8, values: []const RawValue) void {
    const f = @field(T, decl);
    const F = @TypeOf(f);
    var args: std.meta.ArgsTuple(F) = undefined;
    const Self = comptime ParamType(F, 0).?;
    args[0] = if (Self == T) obj.* else obj;
    inline for (1..comptime paramCount(F)) |i| {
        args[i] = argAs(comptime ParamType(F, i).?, values, i - 1);
    }
    const R = @typeInfo(F).@"fn".return_type.?;
    if (@typeInfo(R) == .error_union) {
        _ = @call(.auto, f, args) catch |e| logError("{s}: {s}", .{ decl, @errorName(e) });
    } else {
        _ = @call(.auto, f, args);
    }
}

pub fn hasMethod(comptime classes: anytype, handle: ?*anyopaque, name: [*:0]const u8) c_int {
    const inst: *Instance = @ptrCast(@alignCast(handle orelse return 0));
    const n = std.mem.span(name);
    inline for (classes, 0..) |C, ci| {
        if (inst.class_index == ci) {
            inline for (0..comptime declCount(C.Type)) |di| {
                const dname = comptime declName(C.Type, di);
                if (comptime isMethod(C.Type, dname)) {
                    if (nameMatches(dname, n)) return 1;
                }
            }
        }
    }
    return 0;
}

pub fn invoke(comptime classes: anytype, handle: ?*anyopaque, name: [*:0]const u8, args: ?[*]const RawValue, argc: c_int) c_int {
    const inst: *Instance = @ptrCast(@alignCast(handle orelse return 1));
    const n = std.mem.span(name);
    const values: []const RawValue = if (args) |a| a[0..@intCast(@max(argc, 0))] else &.{};
    inline for (classes, 0..) |C, ci| {
        if (inst.class_index == ci) {
            const obj: *C.Type = @ptrCast(@alignCast(inst.ptr));
            inline for (0..comptime declCount(C.Type)) |di| {
                const dname = comptime declName(C.Type, di);
                if (comptime isMethod(C.Type, dname)) {
                    if (nameMatches(dname, n)) {
                        callMethod(C.Type, obj, dname, values);
                        return 1;
                    }
                }
            }
        }
    }
    return 1;
}

pub fn setField(comptime classes: anytype, handle: ?*anyopaque, name: [*:0]const u8, value: *const RawValue) c_int {
    const inst: *Instance = @ptrCast(@alignCast(handle orelse return 0));
    const n = std.mem.span(name);
    inline for (classes, 0..) |C, ci| {
        if (inst.class_index == ci) {
            const obj: *C.Type = @ptrCast(@alignCast(inst.ptr));
            inline for (0..comptime fieldCount(C.Type)) |fi| {
                const fname = comptime fieldName(C.Type, fi);
                const FT = comptime FieldType(C.Type, fi);
                if (comptime (isInspectorField(fname) and typeName(FT) != null)) {
                    if (std.mem.eql(u8, fname, n)) {
                        assign(FT, &@field(obj, fname), .{ .raw = value.* });
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

pub fn getField(comptime classes: anytype, handle: ?*anyopaque, name: [*:0]const u8, out: *RawValue, type_name: ?*[*:0]const u8) c_int {
    const inst: *Instance = @ptrCast(@alignCast(handle orelse return 0));
    const n = std.mem.span(name);
    inline for (classes, 0..) |C, ci| {
        if (inst.class_index == ci) {
            const obj: *C.Type = @ptrCast(@alignCast(inst.ptr));
            inline for (0..comptime fieldCount(C.Type)) |fi| {
                const fname = comptime fieldName(C.Type, fi);
                const FT = comptime FieldType(C.Type, fi);
                if (comptime (isInspectorField(fname) and typeName(FT) != null)) {
                    if (std.mem.eql(u8, fname, n)) {
                        out.* = toRaw(@field(obj, fname));
                        if (FT == String) {
                            // kopya: alanın kendisi değişebilir
                            text_len = 0;
                            textAppend(@field(obj, fname).slice());
                            out.s = text_buf.ptr;
                        }
                        if (type_name) |t| t.* = typeName(FT).?;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}
