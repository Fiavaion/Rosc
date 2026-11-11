#include "UEcineDeliver.h"

int32 UEcine::Deliver::GetHandleFrames(const FUEcineRenderSpec& Spec)
{
	return Spec.bSlate ? 1 : 0;
}

const TCHAR* UEcine::Deliver::GetFileExtension(EUEcineRenderFormat Format)
{
	return Format == EUEcineRenderFormat::MultilayerExr ? TEXT("exr") : TEXT("mov");
}
