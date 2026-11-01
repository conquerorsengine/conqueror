// JoltPhysics olmadan derlenen motor (CQ_ENABLE_JOLT=OFF; ör. QNX, Jolt bu platformu tanımıyor): 3B fizik arka ucu
// yoktur, sahneler fiziksiz çalışır. MuJoCo arka ucu (çalışma anında yüklenir) varsa yine seçilebilir.
#include "PhysicsBackend3D.h"

namespace Conqueror::Physics3D
{
    std::unique_ptr<Backend> CreateJoltBackend()
    {
        return nullptr;
    }
}
