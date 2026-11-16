// Conqueror Engine HAP giriş yeteneği: tek pencere, içerik pages/Index (tam pencere XComponent + motor modülü)
import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { BusinessError } from '@kit.BasicServicesKit';
import engine from 'lib@CQ_LIBRARY@.so';

const DOMAIN = 0x3C51;
const TAG = 'Conqueror';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, 'EntryAbility onCreate');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err: BusinessError) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, 'Failed to load the content: %{public}s', JSON.stringify(err));
      }
    });
  }

  // 2in1 (PC): pencerenin kapat düğmesi. Motor kendi kapanış akışını yürütür (editör kaydedilmemiş değişiklikleri sorar);
  // motor bitince Index.ets "quit" isteğiyle terminateSelf çağırır
  onPrepareToTerminate(): boolean {
    engine.requestClose();
    return true;
  }

  onDestroy(): void {
    engine.destroy();
  }
}
