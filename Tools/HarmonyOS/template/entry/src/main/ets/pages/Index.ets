// Conqueror Engine sayfası: tam pencere XComponent yüzeyi. XComponent lib@CQ_LIBRARY@.so'yu libraryname ile yükler; motor kendi
// iş parçacığında yüzeye OpenGL ES ile çizer, fare / klavye / dokunma / tekerlek olayları yerel tarafta alınır
// (Engine/src/Platform/OHOS). Motorun sistem arayüzü istekleri (imleç biçimi, belge seçici, adres açma, kapanış) burada yürütülür.
import engine from 'lib@CQ_LIBRARY@.so';
import { common } from '@kit.AbilityKit';
import { fileUri, picker } from '@kit.CoreFileKit';
import { pointer } from '@kit.InputKit';
import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x3C51;
const TAG = 'Conqueror';

// Motorun belge seçici isteği (OHOSModule.cpp ShowPicker)
interface PickRequest {
  id: number;
  mode: string;          // open | openMultiple | save | folder
  extensions: string[];  // ".png" biçiminde, boş = tüm dosyalar
  defaultPath: string;
  defaultName: string;
}

@Entry
@Component
struct Index {
  private context: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
  private windowId: number = -1;

  aboutToAppear(): void {
    engine.setHostCallback((kind: string, payload: string) => {
      this.onHostRequest(kind, payload);
    });
    engine.setup(this.context.resourceManager, this.context.filesDir, this.context.cacheDir, this.context.tempDir);
    window.getLastWindow(this.context).then((win: window.Window) => {
      this.windowId = win.getWindowProperties().id;
    }).catch((err: BusinessError) => {
      hilog.warn(DOMAIN, TAG, 'getLastWindow failed: %{public}s', JSON.stringify(err));
    });
    // Motor iş parçacığı: HAP içeriğini çıkarır ve XComponent yüzeyini bekler
    engine.start([]);
  }

  // Geri hareketi (tablet) ya da Esc dışı geri tuşu: motor kapanışı kendisi yürütür
  onBackPress(): boolean {
    engine.requestClose();
    return true;
  }

  build() {
    Stack() {
      XComponent({ id: 'cq_surface', type: XComponentType.SURFACE, libraryname: '@CQ_LIBRARY@' })
        .width('100%')
        .height('100%')
        .focusable(true)
        .defaultFocus(true)
        .focusOnTouch(true)
    }
    .width('100%')
    .height('100%')
    .backgroundColor(Color.Black)
  }

  private onHostRequest(kind: string, payload: string): void {
    if (kind === 'cursor') {
      this.setCursor(payload);
    } else if (kind === 'openUrl') {
      this.openUrl(payload);
    } else if (kind === 'pick') {
      this.pick(JSON.parse(payload) as PickRequest);
    } else if (kind === 'quit') {
      this.context.terminateSelf();
    }
  }

  private setCursor(name: string): void {
    if (this.windowId < 0) {
      return;
    }
    if (name === 'hidden') {
      pointer.setPointerVisible(false, () => {});
      return;
    }
    pointer.setPointerVisible(true, () => {});
    let style: pointer.PointerStyle = pointer.PointerStyle.DEFAULT;
    switch (name) {
      case 'ibeam': style = pointer.PointerStyle.TEXT_CURSOR; break;
      case 'crosshair': style = pointer.PointerStyle.CROSS; break;
      case 'hand': style = pointer.PointerStyle.HAND_POINTING; break;
      case 'resize_ew': style = pointer.PointerStyle.WEST_EAST; break;
      case 'resize_ns': style = pointer.PointerStyle.NORTH_SOUTH; break;
      case 'resize_nwse': style = pointer.PointerStyle.NORTH_WEST_SOUTH_EAST; break;
      case 'resize_nesw': style = pointer.PointerStyle.NORTH_EAST_SOUTH_WEST; break;
      case 'resize_all': style = pointer.PointerStyle.MOVE; break;
      case 'not_allowed': style = pointer.PointerStyle.CURSOR_FORBID; break;
      default: break;
    }
    pointer.setPointerStyle(this.windowId, style).catch((err: BusinessError) => {
      hilog.warn(DOMAIN, TAG, 'setPointerStyle failed: %{public}s', JSON.stringify(err));
    });
  }

  private openUrl(url: string): void {
    this.context.openLink(url).catch((err: BusinessError) => {
      hilog.warn(DOMAIN, TAG, 'openLink failed: %{public}s', JSON.stringify(err));
    });
  }

  // Sistem belge seçicisi; seçilen URI'ler yola çevrilir (fileUri.FileUri.path)
  private pick(request: PickRequest): void {
    const documentPicker = new picker.DocumentViewPicker(this.context);
    const filters: string[] = request.extensions.length > 0 ? ['Files|' + request.extensions.join(',')] : [];
    const finish = (uris: string[]) => {
      const paths: string[] = uris.map((uri: string) => new fileUri.FileUri(uri).path);
      engine.pickerResult(request.id, paths, '');
    };
    const fail = (err: BusinessError) => {
      engine.pickerResult(request.id, [], 'File picker failed: ' + JSON.stringify(err));
    };
    if (request.mode === 'save') {
      const options = new picker.DocumentSaveOptions();
      if (request.defaultName.length > 0) {
        options.newFileNames = [request.defaultName];
      }
      if (filters.length > 0) {
        options.fileSuffixChoices = filters;
      }
      documentPicker.save(options).then(finish).catch(fail);
      return;
    }
    const options = new picker.DocumentSelectOptions();
    options.maxSelectNumber = request.mode === 'openMultiple' ? 500 : 1;
    if (request.mode === 'folder') {
      options.selectMode = picker.DocumentSelectMode.FOLDER;
    } else if (filters.length > 0) {
      options.fileSuffixFilters = filters;
    }
    documentPicker.select(options).then(finish).catch(fail);
  }
}
