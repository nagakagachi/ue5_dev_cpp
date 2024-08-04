// Copyright Epic Games, Inc. All Rights Reserved.

#include "ue5_dev_cppGameMode.h"
#include "ue5_dev_cppCharacter.h"
#include "UObject/ConstructorHelpers.h"

Aue5_dev_cppGameMode::Aue5_dev_cppGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
