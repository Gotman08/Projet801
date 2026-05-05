// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TileMappingDataAsset.h"
#include "DungeonGenerator.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/**
 * @brief Actor that materialises a WFC-generated dungeon in the level.
 *
 * Drag this Actor into the world, set:
 *   - `JsonPath` to the `dungeon.json` produced by the C++
 *     `wfc_dungeon` CLI;
 *   - `TileMapping` to a `UTileMappingDataAsset` that resolves each
 *     tile id to a static mesh.
 *
 * Then click the "Generate From JSON" button in the Details panel
 * (`UFUNCTION(CallInEditor)`). The actor parses the JSON, iterates
 * the cells, and adds one or more `UStaticMeshComponent` per cell
 * according to the tile id and its cardinal neighbours:
 *
 *   - **Floor** : every walkable tile gets a floor mesh on the cell.
 *   - **Walls** : a wall mesh is added on each cardinal side that
 *     borders a solid neighbour, rotated so the wall faces the cell.
 *   - **Doors** : the wall on a side facing a door tile is replaced
 *     by the door mesh.
 *
 * Click "Clear Generated" to remove every spawned component before
 * regenerating from a different JSON.
 *
 * The actor stores spawned components as child components of itself
 * (rather than spawning separate Actors) so the whole dungeon moves /
 * rotates / scales cleanly with the parent transform.
 */
UCLASS(Blueprintable, ClassGroup = (WFC),
	meta = (BlueprintSpawnableComponent))
class WFCDUNGEON_API ADungeonGenerator : public AActor
{
	GENERATED_BODY()

public:
	ADungeonGenerator();

	/** Path to the JSON file emitted by `wfc_dungeon`. Either an
	 *  absolute path or a path relative to the project's root
	 *  directory (`FPaths::ProjectDir()`). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Input",
		meta = (RelativeToGameDir))
	FFilePath JsonPath;

	/** Mesh assignments. The DataAsset must contain at least every
	 *  tile id present in the JSON, otherwise the corresponding cells
	 *  are skipped (with a warning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Input")
	TObjectPtr<UTileMappingDataAsset> TileMapping = nullptr;

	/** Multiplier applied to the cell size embedded in the JSON. Use
	 *  it to scale the whole dungeon without re-running the generator
	 *  (e.g. shrink to 0.5× for a top-down preview). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout",
		meta = (ClampMin = "0.01"))
	float CellSizeMultiplier = 1.f;

	/** When true, walls and doors are spawned. Disable to inspect
	 *  only the floor layout (useful when debugging asset alignment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout")
	bool bSpawnWalls = true;

	/** When true, an invisible default floor mesh is forced on cells
	 *  whose tile id has no `FloorMesh` set, to preserve continuity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Layout")
	bool bFillFloorGaps = false;

	/** Read JsonPath, parse it, and instantiate the dungeon. Existing
	 *  generated components are cleared first. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void GenerateFromJson();

	/** Remove every component previously created by GenerateFromJson. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "WFC")
	void ClearGenerated();

protected:
	virtual void BeginPlay() override;

private:
	/** Root scene component so the actor has a transform handle. */
	UPROPERTY(VisibleAnywhere, Category = "WFC|Internal")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Tracking list of every component this actor created. Cleared
	 *  on regeneration so old meshes don't leak. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SpawnedComponents;

	/**
	 * @brief Resolve the absolute path of the JSON, looking first at
	 *        `JsonPath` as-is, then relative to ProjectDir.
	 */
	FString ResolveJsonPath() const;

	/**
	 * @brief Spawn the meshes for one cell.
	 *
	 * @param r, c               Cell coordinates inside the grid.
	 * @param TileId             Tile id of this cell.
	 * @param NeighborTileIds    Cardinal neighbours: index 0=N, 1=S,
	 *                           2=E, 3=W. -1 means out-of-bounds.
	 * @param CellCm             Final cell size in cm (already scaled).
	 * @param WallHeightCm       Wall height in cm.
	 */
	void SpawnCell(int32 r, int32 c,
	               int32 TileId,
	               const int32 NeighborTileIds[4],
	               float CellCm,
	               float WallHeightCm);

	/**
	 * @brief Add a single static mesh component at a relative location
	 *        and yaw, applying the variant's offset rotation and scale.
	 */
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh,
	                              const FVector& RelativeLocation,
	                              float YawDegrees,
	                              const FTileVariants& Variant);
};
