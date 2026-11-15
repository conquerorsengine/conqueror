// Conqueror Engine: çok iş parçacıklı web oyuncusu için çapraz köken yalıtımı (SharedArrayBuffer). Sunucu
// Cross-Origin-Opener-Policy / Cross-Origin-Embedder-Policy başlıklarını göndermiyorsa (GitHub Pages, bazı oyun siteleri) bu servis
// çalışanı aynı kökten gelen yanıtlara ekler. Yükleyici (loader.js) kaydeder ve sayfayı bir kez yeniler.
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.cache === "only-if-cached" && request.mode !== "same-origin") return;
  event.respondWith(
    fetch(request).then((response) => {
      if (response.status === 0) return response;
      const headers = new Headers(response.headers);
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
      headers.set("Cross-Origin-Embedder-Policy", "require-corp");
      headers.set("Cross-Origin-Resource-Policy", "cross-origin");
      return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
    }).catch((e) => { console.error(e); return Response.error(); })
  );
});
