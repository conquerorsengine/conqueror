#include "LuaQCompiler.h"
#include "LuaQAst.h"

namespace Conqueror::LuaQ
{
    bool Compile(const std::string& source, const CompileOptions& options, std::string& luauOut, std::vector<Diagnostic>& errors)
    {
        luauOut.clear();
        try
        {
            const auto tokens = Detail::Lex(source);
            const Detail::Chunk chunk = Detail::Parse(tokens);
            Detail::EmitOptions emit;
            emit.AutoReturnClass = options.AutoReturnClass;
            luauOut = Detail::Emit(chunk, emit);
            return true;
        }
        catch (const Detail::CompileError& e)
        {
            errors.push_back({ e.Line, e.Col, e.Message });
            return false;
        }
    }

    const char* Prelude()
    {
        // Luau kaynağı: her LuaQ durumunda modüllerden önce bir kez çalıştırılır
        return R"LUAQ(
local Q = {}

-- ── Sınıflar
local kMeta = { "__tostring", "__eq", "__lt", "__le", "__add", "__sub", "__mul", "__div", "__idiv", "__mod",
                "__pow", "__unm", "__concat", "__len", "__close", "__iter" }

function Q.class(name, base)
    local cls = {}
    cls.__index = cls
    cls.__name = name
    cls.__super = base
    cls.__luaq_class = true
    if base ~= nil then
        for _, k in kMeta do
            local v = rawget(base, k)
            if v ~= nil then cls[k] = v end
        end
    end
    setmetatable(cls, {
        __index = base,
        __call = function(c, ...)
            local o = setmetatable({}, c)
            local init = c.init
            if init ~= nil then init(o, ...) end
            return o
        end,
        __tostring = function() return "class " .. name end,
    })
    return cls
end

-- Örnek bir sınıftan (ya da alt sınıfından) mı: motor örneklerinin metatablosu { __index = sınıf } olabilir
function Q.isinstance(o, cls)
    if type(o) ~= "table" then return false end
    local mt = getmetatable(o)
    if type(mt) ~= "table" then return false end
    local c = if rawget(mt, "__luaq_class") then mt else rawget(mt, "__index")
    while type(c) == "table" do
        if c == cls then return true end
        c = rawget(c, "__super")
    end
    return false
end

-- ── <close> / defer
function Q.close(v, err)
    if v == nil or v == false then return end
    local mt = getmetatable(v)
    local f = type(mt) == "table" and mt.__close or nil
    if f == nil then error("variable got a non-closable value", 2) end
    f(v, err)
end

-- ── {...a, ...b}
function Q.spreadinto(t, src)
    if src == nil then return end
    local n = #t
    local len = #src
    for i = 1, len do t[n + i] = src[i] end
    for k, v in pairs(src) do
        if not (type(k) == "number" and k >= 1 and k <= len and k % 1 == 0) then t[k] = v end
    end
end

-- ── Lua 5.4 matematik eklentileri
function Q.math_type(x)
    if type(x) ~= "number" then return nil end
    return if x % 1 == 0 and x >= -2^53 and x <= 2^53 then "integer" else "float"
end
function Q.math_tointeger(x)
    if type(x) == "number" and x % 1 == 0 then return x end
    if type(x) == "string" then local n = tonumber(x); if n and n % 1 == 0 then return n end end
    return nil
end
function Q.math_ult(a, b)
    if (a < 0) == (b < 0) then return a < b end
    return a >= 0
end

-- ── async / await (eşyordam görevleri; motor her karede Q.tick() çağırır)
local waiting = {}

local function step(task, ...)
    local r = table.pack(coroutine.resume(task.co, ...))
    if not r[1] then
        task.done = true
        task.err = r[2]
        print("[LuaQ] async task error: " .. tostring(r[2]))
        return
    end
    if coroutine.status(task.co) == "dead" then
        task.done = true
        task.result = table.pack(select(2, table.unpack(r, 1, r.n)))
        return
    end
    local req = r[2]
    table.insert(waiting, { task = task, wake = type(req) == "table" and req.wake or nil, other = type(req) == "table" and req.task or nil, start = os.clock() })
end

function Q.async(fn, ...)
    local task = { __task = true, done = false }
    task.co = coroutine.create(fn)
    step(task, ...)
    return task
end

function Q.await(x)
    if not coroutine.isyieldable() then error("await used outside of an async function", 2) end
    local kind = type(x)
    if kind == "number" then
        local t0 = os.clock()
        coroutine.yield({ wake = t0 + x })
        return os.clock() - t0
    elseif kind == "table" and x.__task then
        while not x.done do coroutine.yield({ task = x }) end
        if x.err ~= nil then error(x.err, 0) end
        return table.unpack(x.result, 1, x.result.n)
    elseif kind == "function" then
        return Q.await(Q.async(x))
    elseif x == nil then
        coroutine.yield(nil)
        return nil
    end
    return x
end

function Q.tick()
    if #waiting == 0 then return end
    local now = os.clock()
    local list = waiting
    waiting = {}
    for _, w in list do
        local ready = true
        if w.wake ~= nil then ready = now >= w.wake
        elseif w.other ~= nil then ready = w.other.done end
        if ready then step(w.task) else table.insert(waiting, w) end
    end
end

function Q.pending() return #waiting end

-- Lua 5.4 standart kitaplık eklentileri (Luau'da olmayanlar; tablolar kilitliyse sessizce geçilir)
pcall(function()
    if math.type == nil then math.type = Q.math_type end
    if math.tointeger == nil then math.tointeger = Q.math_tointeger end
    if math.ult == nil then math.ult = Q.math_ult end
    if math.maxinteger == nil then math.maxinteger = 2^53 end
    if math.mininteger == nil then math.mininteger = -2^53 end
end)
if warn == nil then
    warn = function(...) print("[warning]", ...) end
end

__luaq = Q
isinstance = Q.isinstance
spawn = function(fn, ...) return Q.async(fn, ...) end
wait = function(seconds)
    if coroutine.isyieldable() then return Q.await(seconds or 0) end
    error("wait() can only be used inside async functions or spawn()", 2)
end
)LUAQ";
    }
}
