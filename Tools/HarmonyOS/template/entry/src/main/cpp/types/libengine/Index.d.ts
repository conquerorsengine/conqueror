// Conqueror Engine NAPI modülü (Engine/src/Platform/OHOS/OHOSModule.cpp)
import { resourceManager } from '@kit.LocalizationKit';

// Uygulama klasörleri ve HAP kaynak yöneticisi (rawfile içeriği motor iş parçacığında filesDir'e çıkarılır)
export const setup: (resources: resourceManager.ResourceManager, filesDir: string, cacheDir: string, tempDir: string) => void;
// Motor iş parçacığını başlatır (bir kez); args: masaüstü komut satırı argümanları (ör. proje klasörü)
export const start: (args: string[]) => void;
// Motorun istekleri: "cursor" (biçim adı / "hidden"), "openUrl" (adres), "pick" (JSON), "title", "quit" (çıkış kodu)
export const setHostCallback: (callback: (kind: string, payload: string) => void) => void;
// Belge seçicinin sonucu (vazgeçilirse paths boş, hata varsa error dolu)
export const pickerResult: (id: number, paths: string[], error: string) => void;
// Giriş yönteminden metin (UTF-8)
export const insertText: (text: string) => void;
// Pencere kapatılmak isteniyor (editör kaydedilmemiş değişiklikleri sorar)
export const requestClose: () => void;
// Uygulama sonlanıyor
export const destroy: () => void;
