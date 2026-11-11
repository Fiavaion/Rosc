#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/** Root widget of the dockable UEcine panel. Departments are added as tabs inside it. */
class SUEcinePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEcinePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
