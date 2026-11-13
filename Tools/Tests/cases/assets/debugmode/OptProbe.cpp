// 232: Debug Mode (Script Debugging) açıkken betik optimizasyonsuz derlenir (__OPTIMIZE__ tanımsız)
#include <Conqueror/Script.h>
#include "NativeBridge.g.h"

class OptProbe : public cq::Script
{
    CQ_SCRIPT_CLASS(OptProbe)
public:
    void Start() override
    {
#ifdef __OPTIMIZE__
        cq::CreateEntity("Probe_Optimized");
#else
        cq::CreateEntity("Probe_Debug");
#endif
    }
};
CQ_SCRIPT(OptProbe)
