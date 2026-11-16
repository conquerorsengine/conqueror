// Conqueror Engine web yükleyicisi (package_web.py sayfaya gömer). Oyuncuyu (conqueror.js / .wasm) ve oyun verisini indirir,
// gerekirse açar (gzip), tarayıcıda önbellekler (Cache Storage), oyun dosyalarını belleğe (/game) yazar, kayıtları IndexedDB'de
// tutar (/persist: PlayerPrefs, kayıt dosyaları) ve motoru başlatır. Sayfa şunları sağlar: <canvas id="canvas">, isteğe bağlı
// #cq-progress-bar, #cq-status, #cq-loading, #cq-error ve CQ_CONFIG.
(function () {
  "use strict";
  const cfg = window.CQ_CONFIG;
  const $ = (id) => document.getElementById(id);

  function status(text) {
    const s = $("cq-status");
    if (s) s.textContent = text;
  }
  function progress(fraction) {
    const bar = $("cq-progress-bar");
    if (bar) bar.style.width = Math.max(0, Math.min(1, fraction)) * 100 + "%";
  }
  function showError(message) {
    console.error(message);
    const box = $("cq-error");
    if (box) {
      box.textContent = message;
      box.style.display = "block";
    }
    const loading = $("cq-loading");
    if (loading) loading.style.display = "none";
  }
  window.addEventListener("error", (e) => {
    if (e.error && e.error.stack) console.error(e.error.stack);
    showError("Error: " + (e.message || e.error));
  });
  window.addEventListener("unhandledrejection", (e) => showError("Error: " + (e.reason && e.reason.message || e.reason)));

  // Çok iş parçacıklı oyuncu SharedArrayBuffer ister: sayfa COOP / COEP başlıklarıyla gelmediyse servis çalışanı ekler ve yeniler
  if (cfg.threads && !window.crossOriginIsolated) {
    if (!window.isSecureContext || !("serviceWorker" in navigator)) {
      showError("This game uses threads and needs a secure (https or localhost) page with cross-origin isolation.");
      return;
    }
    if (sessionStorage.getItem("cq-coi-reload")) {
      sessionStorage.removeItem("cq-coi-reload");
      showError("Cross-origin isolation could not be enabled. Serve the page with the headers " +
                "Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp.");
      return;
    }
    status("Preparing...");
    navigator.serviceWorker.register("coi-serviceworker.js").then((reg) => {
      const reload = () => { sessionStorage.setItem("cq-coi-reload", "1"); location.reload(); };
      if (reg.active && !navigator.serviceWorker.controller) reload();
      else navigator.serviceWorker.addEventListener("controllerchange", reload);
      if (reg.installing) reg.installing.addEventListener("statechange", (e) => { if (e.target.state === "activated") reload(); });
    }, (err) => showError("Service worker registration failed: " + err));
    return;
  }
  sessionStorage.removeItem("cq-coi-reload");
  if (typeof WebAssembly !== "object") { showError("This browser does not support WebAssembly."); return; }
  {
    const probe = document.createElement("canvas");
    if (!(cfg.webgpu && navigator.gpu) && !probe.getContext("webgl2")) { showError("This browser or graphics driver does not support WebGL 2."); return; }
  }

  async function gunzip(bytes) {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  // İndirme (ilerleme ile) + açma + önbellek
  async function download(url, weight, base, hash) {
    const key = url + "?v=" + hash;
    let cache = null;
    if (cfg.caching && window.caches) {
      try {
        cache = await caches.open("conqueror-" + cfg.name);
        const hit = await cache.match(key);
        if (hit) { progress(base + weight); return new Uint8Array(await hit.arrayBuffer()); }
        for (const old of await cache.keys())
          if (new URL(old.url).pathname === new URL(key, location.href).pathname) await cache.delete(old);
      } catch (e) { cache = null; }
    }
    const response = await fetch(url);
    if (!response.ok) throw new Error("Could not download " + url + " (HTTP " + response.status + ")");
    const total = Number(response.headers.get("Content-Length")) || 0;
    const reader = response.body.getReader();
    const chunks = [];
    let received = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      received += value.length;
      if (total) progress(base + weight * Math.min(1, received / total));
    }
    let bytes = new Uint8Array(received);
    let at = 0;
    for (const c of chunks) { bytes.set(c, at); at += c.length; }
    if (url.endsWith(".gz") && bytes[0] === 0x1f && bytes[1] === 0x8b) bytes = await gunzip(bytes);
    if (url.endsWith(".br") && !(response.headers.get("Content-Encoding") || "").includes("br")) {
      const isWasm = bytes.length >= 4 && bytes[0] === 0x00 && bytes[1] === 0x61 && bytes[2] === 0x73 && bytes[3] === 0x6d;
      if (!url.includes(".wasm") || !isWasm)
        throw new Error("The server does not send .br files with \"Content-Encoding: br\". Configure the server or build with Gzip compression.");
    }
    progress(base + weight);
    if (cache) { try { await cache.put(key, new Response(bytes)); } catch (e) { /* kota */ } }
    return bytes;
  }

  // Taşınabilir oyun paketi (.cqspace, 264): ?game=<adres> ile ya da oyun verisi olmayan oyuncu sitesinde dosya seçiciyle
  function pickPackage() {
    return new Promise((resolve) => {
      status("Open a Conqueror game (.cqspace)");
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".cqspace";
      input.style.cssText = "display:block;margin:16px auto;font-size:16px";
      input.addEventListener("change", async () => {
        const file = input.files && input.files[0];
        if (!file) return;
        input.remove();
        resolve(new Uint8Array(await file.arrayBuffer()));
      });
      ($("status") || document.body).after(input);
    });
  }

  async function start() {
    const gameUrl = new URLSearchParams(location.search).get("game");
    let packageBytes = null;
    if (gameUrl) {
      status("Downloading the game...");
      packageBytes = await download(new URL(gameUrl, location.href).href, 0.45, 0.45, "pkg");
    } else if (!cfg.dataUrl) {
      packageBytes = await pickPackage();
    }
    status("Downloading " + cfg.name + "...");
    const manifest = packageBytes ? { dirs: [], files: [] } : await (await fetch(cfg.manifestUrl, { cache: "no-cache" })).json();
    const [wasmBytes, dataBytes] = await Promise.all([download(cfg.wasmUrl, 0.45, 0, cfg.wasmHash),
                                                      packageBytes ? Promise.resolve(new Uint8Array(0)) : download(cfg.dataUrl, 0.45, 0.45, cfg.dataHash)]);
    status("Starting...");
    await new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = cfg.frameworkUrl;
      s.onload = resolve;
      s.onerror = () => reject(new Error("Could not load " + cfg.frameworkUrl));
      document.body.appendChild(s);
    });

    // Graphics API: WebGPU (tarayıcıda cihaz önceden istenir, istek eşzamansızdır); yoksa ya da başarısızsa WebGL 2
    let gpuDevice = null;
    if (cfg.webgpu && navigator.gpu) {
      try {
        const adapter = await navigator.gpu.requestAdapter({ powerPreference: "high-performance" });
        if (adapter) {
          const wanted = ["float32-filterable", "rg11b10ufloat-renderable", "texture-compression-bc", "depth32float-stencil8"];
          const features = wanted.filter((f) => adapter.features.has(f));
          // Adaptörün izin verdiği en yüksek sınırlar (doku / örnekleyici sayısı, köşe arabellekleri, doku boyu ...)
          const names = ["maxSampledTexturesPerShaderStage", "maxSamplersPerShaderStage", "maxUniformBuffersPerShaderStage",
                         "maxBindingsPerBindGroup", "maxVertexBuffers", "maxVertexAttributes", "maxColorAttachments",
                         "maxTextureDimension2D", "maxTextureArrayLayers", "maxBufferSize", "maxUniformBufferBindingSize",
                         "maxInterStageShaderVariables", "maxColorAttachmentBytesPerSample"];
          const requiredLimits = {};
          for (const n of names) if (adapter.limits[n] !== undefined) requiredLimits[n] = adapter.limits[n];
          gpuDevice = await adapter.requestDevice({ requiredFeatures: features, requiredLimits });
          gpuDevice.lost.then((info) => console.warn("WebGPU device lost: " + info.message));
        }
      } catch (e) {
        console.warn("WebGPU unavailable, using WebGL 2:", e);
        gpuDevice = null;
      }
    }

    const canvas = $("canvas");
    canvas.addEventListener("webglcontextlost", (e) => { e.preventDefault(); showError("The WebGL context was lost. Reload the page."); });
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    // Oyun tuşları (boşluk, oklar, sekme) sayfayı kaydırmasın / odağı taşımasın
    canvas.addEventListener("keydown", (e) => {
      if ([" ", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Tab", "PageUp", "PageDown", "Home", "End"].includes(e.key)) e.preventDefault();
    });
    canvas.addEventListener("mousedown", () => canvas.focus({ preventScroll: true }));
    let syncTimer = 0;
    const Module = {
      canvas: canvas,
      preinitializedWebGPUDevice: gpuDevice,
      arguments: packageBytes ? ["/game.cqspace"] : ["/game"],
      locateFile: (path) => "Build/" + path,
      print: (t) => console.log(t),
      printErr: (t) => console.warn(t),
      instantiateWasm: (imports, done) => {
        WebAssembly.instantiate(wasmBytes, imports).then((r) => done(r.instance, r.module),
          (e) => showError("WebAssembly could not start: " + e));
        return {};
      },
      onAbort: (what) => showError("The game stopped: " + what),
      preRun: [(M) => {
        const FS = M.FS || Module.FS;
        // Oyun dosyaları (ya da tek .cqspace paketi: oyuncu onu açar)
        if (packageBytes) FS.writeFile("/game.cqspace", packageBytes, { canOwn: true });
        FS.mkdirTree("/game");
        for (const d of manifest.dirs) FS.mkdirTree("/game/" + d);
        for (const [path, offset, size] of manifest.files)
          FS.writeFile("/game/" + path, dataBytes.subarray(offset, offset + size), { canOwn: false });
        // Motor oyun dosyalarını çalışma klasörüne göre açar (masaüstünde oyunun klasörü)
        FS.chdir("/game");
        // Kayıtlar (PlayerPrefs, kayıt dosyaları): IndexedDB'de kalıcı
        FS.mkdirTree("/persist");
        try { FS.mount(M.IDBFS || Module.IDBFS || (FS.filesystems && FS.filesystems.IDBFS), {}, "/persist"); }
        catch (e) { console.warn("IDBFS:", e); }
        const env = M.ENV || Module.ENV;
        env.HOME = "/persist";
        env.XDG_DATA_HOME = "/persist/data";
        env.XDG_CONFIG_HOME = "/persist/config";
        env.XDG_CACHE_HOME = "/tmp/cache";
        env.TMPDIR = "/tmp";
        env.CQ_WEB_TEMPLATE = cfg.template || "";
        env.CQ_RENDERER = gpuDevice ? "webgpu" : "opengl";
        // Hata ayıklama: adres çubuğundaki CQ_ ile başlayan değerler ortam değişkeni olur (?CQ_WGPUGL_TRACE=3)
        for (const [k, v] of new URLSearchParams(location.search)) if (/^CQ_[A-Z0-9_]+$/.test(k)) env[k] = v;
        M.addRunDependency("cq-persist");
        FS.syncfs(true, (err) => {
          if (err) console.warn("IDBFS load:", err);
          for (const d of ["/persist/data", "/persist/config", "/tmp/cache"]) try { FS.mkdirTree(d); } catch (e) { }
          M.removeRunDependency("cq-persist");
        });
        const save = () => FS.syncfs(false, (err) => { if (err) console.warn("IDBFS save:", err); });
        syncTimer = setInterval(save, 5000);
        addEventListener("pagehide", save);
        document.addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden") save(); });
      }],
      onRuntimeInitialized: () => {
        const loading = $("cq-loading");
        if (loading) loading.style.display = "none";
        canvas.focus({ preventScroll: true });
      },
    };
    window.cqModule = await createConqueror(Module);
  }

  start().catch((e) => showError(e && e.message ? e.message : String(e)));
})();
