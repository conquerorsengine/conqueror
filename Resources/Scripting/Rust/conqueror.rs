// Conqueror Engine Rust script API. The engine compiles all .rs scripts of a project into one cdylib with rustc
// (no cargo, no external crates). The boundary with the engine is the same C ABI that C, C++ and Zig scripts use.
//
//     use crate::conqueror::*;
//     use crate::nativebridge::*;
//
//     script! {
//         pub struct Spinner {
//             #[range(0.0, 360.0)] speed: f32 = 90.0,   // Inspector field (with a slider)
//             ticks: i32 = 0,
//             label: String = String::from("spin"),
//         }
//         methods [PlayBtn]                              // extra engine / UI events handled in on_message
//     }
//
//     impl Script for Spinner {
//         fn update(&mut self, dt: f32) {
//             let r = cq!(GetRotation);
//             cq!(SetRotation, r.x(), r.y() + (self.speed * dt) as f64, r.z());
//         }
//         fn on_message(&mut self, name: &str, _args: &[Value]) -> bool {
//             if name == "PlayBtn" { self.ticks = 0; return true; }
//             false
//         }
//     }
#![allow(dead_code, non_snake_case)]

use std::cell::RefCell;
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::fmt;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicI32, AtomicPtr, Ordering};

// ── C ABI (identical to the engine's CQNativeValue, 64 bytes) ──
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RawValue {
    pub ty: i32,
    pub dim: i32,
    pub i: i64,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub w: f64,
    pub s: *const c_char,
    pub len: i32,
    pub pad: i32,
}

impl RawValue {
    pub const fn null() -> Self {
        RawValue { ty: 0, dim: 0, i: 0, x: 0.0, y: 0.0, z: 0.0, w: 0.0, s: std::ptr::null(), len: 0, pad: 0 }
    }
}

#[repr(C)]
pub struct HostApi {
    pub version: c_int,
    pub find_native: extern "C" fn(*const c_char) -> c_int,
    pub call_native: extern "C" fn(c_int, c_int, *const RawValue, *mut RawValue) -> c_int,
    pub log: extern "C" fn(c_int, *const c_char),
}

static HOST: AtomicPtr<HostApi> = AtomicPtr::new(std::ptr::null_mut());

fn host() -> Option<&'static HostApi> {
    let p = HOST.load(Ordering::Acquire);
    if p.is_null() { None } else { Some(unsafe { &*p }) }
}

pub fn init(api: *const HostApi) {
    HOST.store(api as *mut HostApi, Ordering::Release);
}

// ── Values ──
#[derive(Clone, Debug, PartialEq, Default)]
pub enum Value {
    #[default]
    Null,
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
    Vector { x: f64, y: f64, z: f64, w: f64, dim: i32 },
    Json(String),
}

impl Value {
    pub fn vec2(x: f64, y: f64) -> Value { Value::Vector { x, y, z: 0.0, w: 0.0, dim: 2 } }
    pub fn vec3(x: f64, y: f64, z: f64) -> Value { Value::Vector { x, y, z, w: 0.0, dim: 3 } }
    pub fn vec4(x: f64, y: f64, z: f64, w: f64) -> Value { Value::Vector { x, y, z, w, dim: 4 } }

    pub fn x(&self) -> f64 { if let Value::Vector { x, .. } = self { *x } else { 0.0 } }
    pub fn y(&self) -> f64 { if let Value::Vector { y, .. } = self { *y } else { 0.0 } }
    pub fn z(&self) -> f64 { if let Value::Vector { z, .. } = self { *z } else { 0.0 } }
    pub fn w(&self) -> f64 { if let Value::Vector { w, .. } = self { *w } else { 0.0 } }

    pub fn is_null(&self) -> bool { matches!(self, Value::Null) }

    pub fn as_f64(&self) -> f64 {
        match self {
            Value::Bool(b) => if *b { 1.0 } else { 0.0 },
            Value::Int(i) => *i as f64,
            Value::Float(f) => *f,
            Value::Str(s) => s.trim().parse().unwrap_or(0.0),
            Value::Vector { x, .. } => *x,
            _ => 0.0,
        }
    }
    pub fn as_f32(&self) -> f32 { self.as_f64() as f32 }
    pub fn as_i64(&self) -> i64 { match self { Value::Int(i) => *i, Value::Bool(b) => *b as i64, _ => self.as_f64() as i64 } }
    pub fn as_i32(&self) -> i32 { self.as_i64() as i32 }
    pub fn as_bool(&self) -> bool {
        match self {
            Value::Null => false,
            Value::Bool(b) => *b,
            Value::Int(i) => *i != 0,
            Value::Float(f) => *f != 0.0,
            Value::Str(s) => !s.is_empty(),
            _ => true,
        }
    }
    pub fn as_str(&self) -> &str { match self { Value::Str(s) | Value::Json(s) => s.as_str(), _ => "" } }

    fn to_raw(&self, keep: &mut Vec<CString>) -> RawValue {
        let mut r = RawValue::null();
        match self {
            Value::Null => {}
            Value::Bool(b) => { r.ty = 1; r.i = *b as i64; }
            Value::Int(i) => { r.ty = 2; r.i = *i; }
            Value::Float(f) => { r.ty = 3; r.x = *f; }
            Value::Str(s) | Value::Json(s) => {
                let c = CString::new(s.replace('\0', "")).unwrap_or_default();
                r.ty = if matches!(self, Value::Json(_)) { 6 } else { 4 };
                r.s = c.as_ptr();
                r.len = c.as_bytes().len() as i32;
                keep.push(c);
            }
            Value::Vector { x, y, z, w, dim } => { r.ty = 5; r.dim = *dim; r.x = *x; r.y = *y; r.z = *z; r.w = *w; }
        }
        r
    }

    pub fn from_raw(r: &RawValue) -> Value {
        let text = || -> String {
            if r.s.is_null() { return String::new(); }
            let bytes = unsafe { std::slice::from_raw_parts(r.s as *const u8, r.len.max(0) as usize) };
            String::from_utf8_lossy(bytes).into_owned()
        };
        match r.ty {
            1 => Value::Bool(r.i != 0),
            2 => Value::Int(r.i),
            3 => Value::Float(r.x),
            4 => Value::Str(text()),
            5 => Value::Vector { x: r.x, y: r.y, z: r.z, w: r.w, dim: r.dim },
            6 => Value::Json(text()),
            _ => Value::Null,
        }
    }
}

impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Value::Null => write!(f, "null"),
            Value::Bool(b) => write!(f, "{}", b),
            Value::Int(i) => write!(f, "{}", i),
            Value::Float(v) => write!(f, "{}", v),
            Value::Str(s) | Value::Json(s) => write!(f, "{}", s),
            Value::Vector { x, y, z, w, dim } => match dim {
                2 => write!(f, "({}, {})", x, y),
                4 => write!(f, "({}, {}, {}, {})", x, y, z, w),
                _ => write!(f, "({}, {}, {})", x, y, z),
            },
        }
    }
}

macro_rules! value_from {
    ($($t:ty => $v:expr),* $(,)?) => { $( impl From<$t> for Value { fn from(x: $t) -> Value { ($v)(x) } } )* };
}
value_from! {
    bool => Value::Bool,
    i8 => |x: i8| Value::Int(x as i64), i16 => |x: i16| Value::Int(x as i64), i32 => |x: i32| Value::Int(x as i64), i64 => Value::Int,
    u8 => |x: u8| Value::Int(x as i64), u16 => |x: u16| Value::Int(x as i64), u32 => |x: u32| Value::Int(x as i64),
    u64 => |x: u64| Value::Int(x as i64), usize => |x: usize| Value::Int(x as i64), isize => |x: isize| Value::Int(x as i64),
    f32 => |x: f32| Value::Float(x as f64), f64 => Value::Float,
    String => Value::Str, &str => |x: &str| Value::Str(x.to_string()), &String => |x: &String| Value::Str(x.clone()),
    () => |_| Value::Null,
}
impl From<&Value> for Value { fn from(v: &Value) -> Value { v.clone() } }

// ── Engine calls ──
pub fn find_native(name: &str) -> i32 {
    match (host(), CString::new(name)) {
        (Some(h), Ok(c)) => (h.find_native)(c.as_ptr()),
        _ => -1,
    }
}

pub fn call_index(index: i32, args: &[Value]) -> Value {
    let Some(h) = host() else { return Value::Null };
    if index < 0 { return Value::Null; }
    let mut keep = Vec::new();
    let raw: Vec<RawValue> = args.iter().map(|a| a.to_raw(&mut keep)).collect();
    let mut result = RawValue::null();
    if (h.call_native)(index, raw.len() as c_int, raw.as_ptr(), &mut result) == 0 { return Value::Null; }
    Value::from_raw(&result) // strings are copied immediately
}

/// Call by name with a cached index (the generated nativebridge functions use this)
pub fn call_cached(cache: &AtomicI32, name: &str, args: &[Value]) -> Value {
    let mut i = cache.load(Ordering::Relaxed);
    if i == -2 {
        i = find_native(name);
        cache.store(i, Ordering::Relaxed);
    }
    call_index(i, args)
}

/// Call any engine function by name: call("GetPosition", &[])
pub fn call(name: &str, args: &[Value]) -> Value { call_index(find_native(name), args) }

fn log_level(level: c_int, message: &str) {
    if let (Some(h), Ok(c)) = (host(), CString::new(message.replace('\0', ""))) { (h.log)(level, c.as_ptr()); }
}
pub fn log(message: impl AsRef<str>) { log_level(0, message.as_ref()); }
pub fn log_warning(message: impl AsRef<str>) { log_level(1, message.as_ref()); }
pub fn log_error(message: impl AsRef<str>) { log_level(2, message.as_ref()); }

/// Engine call with automatic argument conversion: cq!(SetRotation, x, y + 1.0, z)
#[macro_export]
macro_rules! cq {
    ($f:ident $(, $a:expr)* $(,)?) => { $crate::nativebridge::$f(&[ $( $crate::conqueror::Value::from($a) ),* ]) };
}

// ── Scripts ──
pub trait Script {
    fn awake(&mut self) {}
    fn start(&mut self) {}
    fn update(&mut self, _dt: f32) {}
    fn late_update(&mut self, _dt: f32) {}
    fn on_destroy(&mut self) {}
    /// Other engine events and UI callbacks (names listed in `methods [...]`); return true when handled
    fn on_message(&mut self, _name: &str, _args: &[Value]) -> bool { false }
}

pub struct FieldDesc {
    pub name: &'static str,
    pub ty: &'static str,
    pub default: Value,
    pub attributes: &'static str,
}

pub trait FieldType: Sized {
    const NAME: &'static str;
    fn to_value(&self) -> Value;
    fn from_value(v: &Value) -> Self;
}
impl FieldType for f32 { const NAME: &'static str = "float"; fn to_value(&self) -> Value { Value::Float(*self as f64) } fn from_value(v: &Value) -> Self { v.as_f32() } }
impl FieldType for f64 { const NAME: &'static str = "double"; fn to_value(&self) -> Value { Value::Float(*self) } fn from_value(v: &Value) -> Self { v.as_f64() } }
impl FieldType for i32 { const NAME: &'static str = "int"; fn to_value(&self) -> Value { Value::Int(*self as i64) } fn from_value(v: &Value) -> Self { v.as_i32() } }
impl FieldType for i64 { const NAME: &'static str = "int"; fn to_value(&self) -> Value { Value::Int(*self) } fn from_value(v: &Value) -> Self { v.as_i64() } }
impl FieldType for u32 { const NAME: &'static str = "int"; fn to_value(&self) -> Value { Value::Int(*self as i64) } fn from_value(v: &Value) -> Self { v.as_i64().max(0) as u32 } }
impl FieldType for bool { const NAME: &'static str = "bool"; fn to_value(&self) -> Value { Value::Bool(*self) } fn from_value(v: &Value) -> Self { v.as_bool() } }
impl FieldType for String {
    const NAME: &'static str = "string";
    fn to_value(&self) -> Value { Value::Str(self.clone()) }
    fn from_value(v: &Value) -> Self { match v { Value::Str(s) => s.clone(), other => other.to_string() } }
}

pub trait Reflect {
    fn class_name() -> &'static str where Self: Sized;
    fn fields() -> Vec<FieldDesc> where Self: Sized;
    fn methods() -> &'static [&'static str] where Self: Sized;
    fn get_field(&self, name: &str) -> Option<(Value, &'static str)>;
    fn set_field(&mut self, name: &str, value: &Value) -> bool;
    fn set_entity(&mut self, entity: u32);
}

pub trait ScriptObject: Script + Reflect {}
impl<T: Script + Reflect> ScriptObject for T {}

#[macro_export]
macro_rules! __cq_range {
    () => { "" };
    ($lo:expr, $hi:expr) => { concat!("Range(", stringify!($lo), ", ", stringify!($hi), ")") };
}

/// Declares a script class: fields with defaults (shown in the Inspector) and optional extra event names
#[macro_export]
macro_rules! script {
    (
        $(#[$meta:meta])*
        pub struct $name:ident {
            $( $(#[range($lo:expr, $hi:expr)])? $field:ident : $ty:ty = $default:expr ),* $(,)?
        }
        $( methods [ $( $method:ident ),* $(,)? ] )?
    ) => {
        $(#[$meta])*
        pub struct $name {
            $( pub $field: $ty, )*
            pub entity: u32,
        }
        impl Default for $name {
            fn default() -> Self { $name { $( $field: $default, )* entity: u32::MAX } }
        }
        impl $crate::conqueror::Reflect for $name {
            fn class_name() -> &'static str { stringify!($name) }
            fn fields() -> Vec<$crate::conqueror::FieldDesc> {
                let d = <$name as Default>::default();
                vec![ $( $crate::conqueror::FieldDesc {
                    name: stringify!($field),
                    ty: <$ty as $crate::conqueror::FieldType>::NAME,
                    default: $crate::conqueror::FieldType::to_value(&d.$field),
                    attributes: $crate::__cq_range!($($lo, $hi)?),
                } ),* ]
            }
            fn methods() -> &'static [&'static str] { &[ $( $( stringify!($method) ),* )? ] }
            fn get_field(&self, name: &str) -> Option<($crate::conqueror::Value, &'static str)> {
                match name {
                    $( stringify!($field) => Some(($crate::conqueror::FieldType::to_value(&self.$field), <$ty as $crate::conqueror::FieldType>::NAME)), )*
                    _ => None,
                }
            }
            fn set_field(&mut self, name: &str, value: &$crate::conqueror::Value) -> bool {
                match name {
                    $( stringify!($field) => { self.$field = <$ty as $crate::conqueror::FieldType>::from_value(value); true } )*
                    _ => false,
                }
            }
            fn set_entity(&mut self, entity: u32) { self.entity = entity; }
        }
    };
}

// ── Plugin entry points (instantiated once by the generated lib.rs) ──
pub struct ClassEntry {
    pub name: &'static str,
    pub create: fn() -> Box<dyn ScriptObject>,
    pub fields: fn() -> Vec<FieldDesc>,
    pub methods: fn() -> &'static [&'static str],
}

impl ClassEntry {
    pub fn of<T: ScriptObject + Default + 'static>() -> ClassEntry {
        ClassEntry {
            name: T::class_name(),
            create: || Box::new(T::default()),
            fields: T::fields,
            methods: T::methods,
        }
    }
}

struct Instance {
    object: Box<dyn ScriptObject>,
    class: &'static ClassEntry,
}

thread_local! {
    static TEXT: RefCell<CString> = RefCell::new(CString::default());
    static ERROR: RefCell<CString> = RefCell::new(CString::default());
}

fn keep_text(s: String) -> *const c_char {
    TEXT.with(|t| {
        *t.borrow_mut() = CString::new(s.replace('\0', "")).unwrap_or_default();
        t.borrow().as_ptr()
    })
}

fn set_error(s: String) {
    ERROR.with(|e| *e.borrow_mut() = CString::new(s.replace('\0', "")).unwrap_or_default());
}

fn json_string(s: &str) -> String {
    let mut o = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            c if (c as u32) < 0x20 => o.push_str(&format!("\\u{:04x}", c as u32)),
            c => o.push(c),
        }
    }
    o.push('"');
    o
}

fn cstr(p: *const c_char) -> &'static str {
    if p.is_null() { return ""; }
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
}

fn panic_text(e: Box<dyn std::any::Any + Send>) -> String {
    if let Some(s) = e.downcast_ref::<&str>() { s.to_string() }
    else if let Some(s) = e.downcast_ref::<String>() { s.clone() }
    else { "panic".to_string() }
}

pub fn describe(classes: &'static [ClassEntry]) -> *const c_char {
    let mut out = String::from("[");
    for (ci, c) in classes.iter().enumerate() {
        if ci > 0 { out.push(','); }
        out.push_str(&format!("{{\"name\":{},\"fields\":[", json_string(c.name)));
        for (fi, f) in (c.fields)().iter().enumerate() {
            if fi > 0 { out.push(','); }
            let def = match &f.default {
                Value::Str(s) => json_string(s),
                Value::Bool(b) => b.to_string(),
                Value::Int(i) => i.to_string(),
                Value::Float(v) => if v.is_finite() { format!("{:?}", v) } else { "0".to_string() },
                _ => "null".to_string(),
            };
            out.push_str(&format!("{{\"name\":{},\"type\":\"{}\",\"default\":{},\"attributes\":[{}]}}",
                json_string(f.name), f.ty, def, if f.attributes.is_empty() { String::new() } else { json_string(f.attributes) }));
        }
        out.push_str("]}");
    }
    out.push(']');
    keep_text(out)
}

pub fn create(classes: &'static [ClassEntry], name: *const c_char, entity: u32) -> *mut c_void {
    let name = cstr(name);
    let Some(class) = classes.iter().find(|c| c.name == name) else {
        set_error(format!("Rust script class '{}' not found (declare it with script! {{ pub struct {} {{ ... }} }})", name, name));
        return std::ptr::null_mut();
    };
    match catch_unwind(AssertUnwindSafe(|| (class.create)())) {
        Ok(mut object) => {
            object.set_entity(entity);
            Box::into_raw(Box::new(Instance { object, class })) as *mut c_void
        }
        Err(e) => { set_error(format!("{}: {}", name, panic_text(e))); std::ptr::null_mut() }
    }
}

pub fn destroy(handle: *mut c_void) {
    if !handle.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe { drop(Box::from_raw(handle as *mut Instance)) }));
    }
}

const LIFECYCLE: [&str; 5] = ["Awake", "Start", "Update", "LateUpdate", "OnDestroy"];

pub fn has_method(handle: *mut c_void, name: *const c_char) -> c_int {
    if handle.is_null() { return 0; }
    let inst = unsafe { &*(handle as *mut Instance) };
    let name = cstr(name);
    (LIFECYCLE.contains(&name) || (inst.class.methods)().contains(&name)) as c_int
}

pub fn invoke(handle: *mut c_void, name: *const c_char, args: *const RawValue, argc: c_int) -> c_int {
    if handle.is_null() { return 1; }
    let inst = unsafe { &mut *(handle as *mut Instance) };
    let name = cstr(name);
    let values: Vec<Value> = if args.is_null() || argc <= 0 { Vec::new() }
        else { unsafe { std::slice::from_raw_parts(args, argc as usize) }.iter().map(Value::from_raw).collect() };
    let dt = values.first().map(|v| v.as_f32()).unwrap_or(0.0);
    let result = catch_unwind(AssertUnwindSafe(|| {
        let s = &mut inst.object;
        match name {
            "Awake" => s.awake(),
            "Start" => s.start(),
            "Update" => s.update(dt),
            "LateUpdate" => s.late_update(dt),
            "OnDestroy" => s.on_destroy(),
            _ => { s.on_message(name, &values); }
        }
    }));
    match result {
        Ok(()) => 1,
        Err(e) => { set_error(format!("{}: panic: {}", name, panic_text(e))); 0 }
    }
}

pub fn set_field(handle: *mut c_void, name: *const c_char, value: *const RawValue) -> c_int {
    if handle.is_null() || value.is_null() { return 0; }
    let inst = unsafe { &mut *(handle as *mut Instance) };
    let v = Value::from_raw(unsafe { &*value });
    inst.object.set_field(cstr(name), &v) as c_int
}

pub fn get_field(handle: *mut c_void, name: *const c_char, out: *mut RawValue, type_name: *mut *const c_char) -> c_int {
    if handle.is_null() || out.is_null() { return 0; }
    let inst = unsafe { &*(handle as *mut Instance) };
    let Some((v, ty)) = inst.object.get_field(cstr(name)) else { return 0 };
    let mut raw = RawValue::null();
    match &v {
        Value::Str(s) => { raw.ty = 4; raw.s = keep_text(s.clone()); raw.len = s.len() as i32; }
        other => { let mut keep = Vec::new(); raw = other.to_raw(&mut keep); }
    }
    unsafe { *out = raw; }
    if !type_name.is_null() {
        let t: &'static [u8] = match ty { "float" => b"float\0", "double" => b"double\0", "bool" => b"bool\0", "string" => b"string\0", _ => b"int\0" };
        unsafe { *type_name = t.as_ptr() as *const c_char; }
    }
    1
}

pub fn last_error() -> *const c_char { ERROR.with(|e| e.borrow().as_ptr()) }

/// Generates the C entry points for the listed script classes (used by the generated lib.rs)
#[macro_export]
macro_rules! export_plugin {
    ( $( $ty:path ),* $(,)? ) => {
        fn __cq_classes() -> &'static [$crate::conqueror::ClassEntry] {
            static CLASSES: std::sync::OnceLock<Vec<$crate::conqueror::ClassEntry>> = std::sync::OnceLock::new();
            CLASSES.get_or_init(|| vec![ $( $crate::conqueror::ClassEntry::of::<$ty>() ),* ]).as_slice()
        }
        #[no_mangle] pub extern "C" fn cq_plugin_version() -> std::ffi::c_int { 1 }
        #[no_mangle] pub extern "C" fn cq_plugin_init(api: *const $crate::conqueror::HostApi) { $crate::conqueror::init(api) }
        #[no_mangle] pub extern "C" fn cq_plugin_describe() -> *const std::ffi::c_char { $crate::conqueror::describe(__cq_classes()) }
        #[no_mangle] pub extern "C" fn cq_create(name: *const std::ffi::c_char, entity: u32) -> *mut std::ffi::c_void { $crate::conqueror::create(__cq_classes(), name, entity) }
        #[no_mangle] pub extern "C" fn cq_destroy(handle: *mut std::ffi::c_void) { $crate::conqueror::destroy(handle) }
        #[no_mangle] pub extern "C" fn cq_has_method(handle: *mut std::ffi::c_void, name: *const std::ffi::c_char) -> std::ffi::c_int { $crate::conqueror::has_method(handle, name) }
        #[no_mangle] pub extern "C" fn cq_invoke(handle: *mut std::ffi::c_void, name: *const std::ffi::c_char, args: *const $crate::conqueror::RawValue, argc: std::ffi::c_int) -> std::ffi::c_int { $crate::conqueror::invoke(handle, name, args, argc) }
        #[no_mangle] pub extern "C" fn cq_set_field(handle: *mut std::ffi::c_void, name: *const std::ffi::c_char, value: *const $crate::conqueror::RawValue) -> std::ffi::c_int { $crate::conqueror::set_field(handle, name, value) }
        #[no_mangle] pub extern "C" fn cq_get_field(handle: *mut std::ffi::c_void, name: *const std::ffi::c_char, out: *mut $crate::conqueror::RawValue, type_name: *mut *const std::ffi::c_char) -> std::ffi::c_int { $crate::conqueror::get_field(handle, name, out, type_name) }
        #[no_mangle] pub extern "C" fn cq_last_error() -> *const std::ffi::c_char { $crate::conqueror::last_error() }
    };
}
