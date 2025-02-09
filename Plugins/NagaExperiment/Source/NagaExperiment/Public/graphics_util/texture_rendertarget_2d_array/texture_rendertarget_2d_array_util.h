// Copyright nagakagachi. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "texture_rendertarget_2d_array_util.generated.h"

UCLASS()
class NAGAEXPERIMENT_API UTextureRenderTarget2dArrayUtilBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()


	// RenderTargetArrayの指定したSliceに対してDrawMaterialToRenderTargetを実行します. 内部処理に一時リソース生成やテクスチャコピーが含まれます.
	// Performs a DrawMaterial on the specified TextureArray Slice. This method creates a Temporal Texture and executes CopyTexture.
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Draw Material To Texture RenderTarget Array Slice", Keywords = "DrawMaterialToRenderTarget2dArraySlice", WorldContext = "WorldContextObject"), Category = "NagaExperiment")
	static void DrawMaterialToRenderTarget2dArraySlice(UObject* WorldContextObject, UMaterialInterface* Material, class UTextureRenderTarget2DArray* TextureRt2dArray, int SliceIndex = 0);

};
