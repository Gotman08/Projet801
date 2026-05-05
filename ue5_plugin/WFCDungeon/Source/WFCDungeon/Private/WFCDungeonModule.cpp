// SPDX-License-Identifier: MIT
#include "WFCDungeonModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogWFCDungeon, Log, All);

void FWFCDungeonModule::StartupModule()
{
	UE_LOG(LogWFCDungeon, Log, TEXT("WFCDungeon module started"));
}

void FWFCDungeonModule::ShutdownModule()
{
	UE_LOG(LogWFCDungeon, Log, TEXT("WFCDungeon module shut down"));
}

IMPLEMENT_MODULE(FWFCDungeonModule, WFCDungeon)
