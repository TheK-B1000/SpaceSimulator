// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorHudWidget.h"

void UEdenOperatorHudWidget::ApplyHudSnapshot(const FEdenOperatorHudSnapshot& InSnapshot)
{
	CurrentSnapshot = InSnapshot;
	OnHudSnapshotUpdated(InSnapshot);
}

FEdenOperatorHudSnapshot UEdenOperatorHudWidget::GetHudSnapshot() const
{
	return CurrentSnapshot;
}
