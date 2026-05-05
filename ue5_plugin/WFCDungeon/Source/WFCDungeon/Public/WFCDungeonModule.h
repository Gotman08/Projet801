// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module of the WFC Dungeon plugin.
 *
 * Empty StartupModule/ShutdownModule for now, all the user-facing
 * functionality lives in the Actor and DataAsset classes. Keeping a
 * concrete module class makes it cheap to add log categories or
 * console commands later without breaking existing projects.
 */
class FWFCDungeonModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
