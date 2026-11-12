// Copyright 2026 Fiavaion. All rights reserved.

#include "UEcinePaperwork.h"

#include "Misc/Base64.h"
#include "UEcineMarkup.h"

#define LOCTEXT_NAMESPACE "UEcineCore"

namespace
{
	/** One column of a table of shots. */
	struct FPaperworkColumn
	{
		FText Heading;
		FString FUEcineShotReport::* Field;
	};

	TArray<FPaperworkColumn> GetShotListColumns()
	{
		return
		{
			{ LOCTEXT("ColumnScene", "Scene"), &FUEcineShotReport::Scene },
			{ LOCTEXT("ColumnShot", "Shot"), &FUEcineShotReport::Shot },
			{ LOCTEXT("ColumnSize", "Size"), &FUEcineShotReport::Size },
			{ LOCTEXT("ColumnMove", "Move"), &FUEcineShotReport::Move },
			{ LOCTEXT("ColumnLens", "Lens"), &FUEcineShotReport::FocalLength },
			{ LOCTEXT("ColumnSupport", "Support"), &FUEcineShotReport::Support },
			{ LOCTEXT("ColumnStart", "Start"), &FUEcineShotReport::Start },
			{ LOCTEXT("ColumnDuration", "Duration"), &FUEcineShotReport::Duration },
			{ LOCTEXT("ColumnNotes", "Notes"), &FUEcineShotReport::Notes },
		};
	}

	TArray<FPaperworkColumn> GetCameraReportColumns()
	{
		return
		{
			{ LOCTEXT("ColumnScene", "Scene"), &FUEcineShotReport::Scene },
			{ LOCTEXT("ColumnShot", "Shot"), &FUEcineShotReport::Shot },
			{ LOCTEXT("ColumnCamera", "Camera"), &FUEcineShotReport::Camera },
			{ LOCTEXT("ColumnBody", "Body"), &FUEcineShotReport::Body },
			{ LOCTEXT("ColumnFormat", "Recording format"), &FUEcineShotReport::Format },
			{ LOCTEXT("ColumnLensKit", "Lens kit"), &FUEcineShotReport::LensKit },
			{ LOCTEXT("ColumnFocalLength", "Focal length"), &FUEcineShotReport::FocalLength },
			{ LOCTEXT("ColumnTStop", "T-stop"), &FUEcineShotReport::TStop },
			{ LOCTEXT("ColumnIso", "ISO"), &FUEcineShotReport::Iso },
			{ LOCTEXT("ColumnShutter", "Shutter angle"), &FUEcineShotReport::Shutter },
			{ LOCTEXT("ColumnFrameRate", "Frame rate"), &FUEcineShotReport::FrameRate },
			{ LOCTEXT("ColumnNd", "ND"), &FUEcineShotReport::Nd },
			{ LOCTEXT("ColumnFocus", "Focus"), &FUEcineShotReport::Focus },
			{ LOCTEXT("ColumnSupport", "Support"), &FUEcineShotReport::Support },
			{ LOCTEXT("ColumnStart", "Start"), &FUEcineShotReport::Start },
			{ LOCTEXT("ColumnDuration", "Duration"), &FUEcineShotReport::Duration },
			{ LOCTEXT("ColumnNotes", "Notes"), &FUEcineShotReport::Notes },
		};
	}

	/** One field of a comma-separated file, quoted when it has to be (RFC 4180). */
	FString EscapeCsvField(const FString& Field)
	{
		FString Value = Field;
		// A spreadsheet runs a cell that starts like a formula. An apostrophe makes it plain text.
		if (!Value.IsEmpty() && (Value[0] == TEXT('=') || Value[0] == TEXT('+') || Value[0] == TEXT('-') || Value[0] == TEXT('@')))
		{
			Value.InsertAt(0, TEXT('\''));
		}
		if (Value.Contains(TEXT(",")) || Value.Contains(TEXT("\"")) || Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\r")))
		{
			Value = TEXT("\"") + Value.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
		}
		return Value;
	}

	FString WriteCsv(const FUEcinePaperwork& Paperwork, const TArray<FPaperworkColumn>& Columns)
	{
		FString Csv;
		for (int32 ColumnIndex = 0; ColumnIndex < Columns.Num(); ++ColumnIndex)
		{
			Csv += (ColumnIndex > 0 ? TEXT(",") : TEXT("")) + EscapeCsvField(Columns[ColumnIndex].Heading.ToString());
		}
		Csv += TEXT("\r\n");
		for (const FUEcineShotReport& Shot : Paperwork.Shots)
		{
			for (int32 ColumnIndex = 0; ColumnIndex < Columns.Num(); ++ColumnIndex)
			{
				Csv += (ColumnIndex > 0 ? TEXT(",") : TEXT("")) + EscapeCsvField(Shot.*(Columns[ColumnIndex].Field));
			}
			Csv += TEXT("\r\n");
		}
		return Csv;
	}

	/** Text for a web page, keeping the line breaks the user typed. */
	FString EscapeHtmlText(const FString& Text)
	{
		return UEcine::Markup::Escape(Text).Replace(TEXT("\r\n"), TEXT("<br>")).Replace(TEXT("\n"), TEXT("<br>"));
	}

	/** Black on white, a plain system typeface, and each shot kept on one printed page where it fits. */
	const TCHAR* PaperworkStyle = TEXT(
		"body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;color:#111;background:#fff;margin:24px;line-height:1.4}\n"
		"h1{font-size:22px;margin:0}h2{font-size:17px;margin:28px 0 8px}h3{font-size:15px;margin:0 0 8px}\n"
		".meta{color:#444;margin:4px 0 0}\n"
		"table{border-collapse:collapse;width:100%;font-size:13px}\n"
		"th,td{border:1px solid #777;padding:4px 8px;text-align:left;vertical-align:top}\n"
		"th{background:#eee}\n"
		".shot{border-top:2px solid #111;margin-top:24px;padding-top:12px;break-inside:avoid}\n"
		".pictures{display:flex;flex-wrap:wrap;gap:12px;margin:0 0 12px}\n"
		"figure{margin:0}figcaption{font-size:12px;color:#444}\n"
		".pictures img{max-width:320px;height:auto;border:1px solid #777;display:block}\n"
		".plan svg{max-width:100%;height:auto;border:1px solid #777}\n"
		".report{width:auto;margin-top:12px}.report th{width:160px}\n"
		".notes{margin:12px 0 0}\n"
		"@media print{body{margin:0}}\n");
}

FString UEcine::Paperwork::WriteShotListCsv(const FUEcinePaperwork& Paperwork)
{
	return WriteCsv(Paperwork, GetShotListColumns());
}

FString UEcine::Paperwork::WriteCameraReportCsv(const FUEcinePaperwork& Paperwork)
{
	return WriteCsv(Paperwork, GetCameraReportColumns());
}

FString UEcine::Paperwork::WriteHtml(const FUEcinePaperwork& Paperwork)
{
	using UEcine::Markup::Escape;

	const FString Title = FText::Format(LOCTEXT("PaperworkTitle", "{0}: shot list and camera report"), FText::FromString(Paperwork.Title)).ToString();

	FString Html = TEXT("<!DOCTYPE html>\n");
	Html += FString::Printf(TEXT("<html lang=\"%s\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>%s</title>\n<style>\n%s</style>\n</head>\n<body>\n"),
		*Escape(Paperwork.Language), *Escape(Title), PaperworkStyle);
	Html += FString::Printf(TEXT("<h1>%s</h1>\n<p class=\"meta\">%s</p>\n"), *Escape(Title), *Escape(Paperwork.Date));

	// The shot list.
	const TArray<FPaperworkColumn> ListColumns = GetShotListColumns();
	Html += FString::Printf(TEXT("<h2>%s</h2>\n<table>\n<thead><tr>"), *Escape(LOCTEXT("ShotListHeading", "Shot list").ToString()));
	for (const FPaperworkColumn& Column : ListColumns)
	{
		Html += FString::Printf(TEXT("<th scope=\"col\">%s</th>"), *Escape(Column.Heading.ToString()));
	}
	Html += TEXT("</tr></thead>\n<tbody>\n");
	for (const FUEcineShotReport& Shot : Paperwork.Shots)
	{
		Html += TEXT("<tr>");
		for (const FPaperworkColumn& Column : ListColumns)
		{
			Html += FString::Printf(TEXT("<td>%s</td>"), *EscapeHtmlText(Shot.*(Column.Field)));
		}
		Html += TEXT("</tr>\n");
	}
	Html += TEXT("</tbody>\n</table>\n");

	// Each shot: storyboard, overhead plan, camera report.
	Html += FString::Printf(TEXT("<h2>%s</h2>\n"), *Escape(LOCTEXT("ShotsHeading", "Shots").ToString()));
	for (const FUEcineShotReport& Shot : Paperwork.Shots)
	{
		TArray<FString> HeadingParts;
		if (!Shot.Scene.IsEmpty())
		{
			HeadingParts.Add(FText::Format(LOCTEXT("ShotHeadingScene", "Scene {0}"), FText::FromString(Shot.Scene)).ToString());
		}
		HeadingParts.Add(Shot.Shot);
		if (!Shot.Size.IsEmpty())
		{
			HeadingParts.Add(Shot.Size);
		}
		const FString Heading = FString::Join(HeadingParts, TEXT(" · "));
		Html += FString::Printf(TEXT("<section class=\"shot\">\n<h3>%s</h3>\n"), *Escape(Heading));

		if (!Shot.Storyboard.IsEmpty())
		{
			Html += TEXT("<div class=\"pictures\">\n");
			for (const FUEcinePaperworkPicture& Picture : Shot.Storyboard)
			{
				const FString AltText = FText::Format(LOCTEXT("StoryboardAlt", "Storyboard frame of {0}: {1}"), FText::FromString(Shot.Shot), FText::FromString(Picture.Caption)).ToString();
				Html += FString::Printf(TEXT("<figure><img alt=\"%s\" src=\"data:image/jpeg;base64,%s\"><figcaption>%s</figcaption></figure>\n"),
					*Escape(AltText), *FBase64::Encode(Picture.Jpeg), *Escape(Picture.Caption));
			}
			Html += TEXT("</div>\n");
		}

		if (!Shot.PlanSvg.IsEmpty())
		{
			Html += FString::Printf(TEXT("<figure class=\"plan\">\n%s<figcaption>%s</figcaption></figure>\n"),
				*Shot.PlanSvg, *Escape(LOCTEXT("PlanCaption", "Overhead plan").ToString()));
		}

		Html += TEXT("<table class=\"report\">\n");
		for (const FPaperworkColumn& Column : GetCameraReportColumns())
		{
			const FString& Value = Shot.*(Column.Field);
			const bool bInHeading = Column.Field == &FUEcineShotReport::Scene || Column.Field == &FUEcineShotReport::Shot;
			if (!Value.IsEmpty() && !bInHeading && Column.Field != &FUEcineShotReport::Notes)
			{
				Html += FString::Printf(TEXT("<tr><th scope=\"row\">%s</th><td>%s</td></tr>\n"), *Escape(Column.Heading.ToString()), *EscapeHtmlText(Value));
			}
		}
		Html += TEXT("</table>\n");

		if (!Shot.Notes.IsEmpty())
		{
			Html += FString::Printf(TEXT("<p class=\"notes\">%s</p>\n"), *EscapeHtmlText(Shot.Notes));
		}
		Html += TEXT("</section>\n");
	}

	Html += TEXT("</body>\n</html>\n");
	return Html;
}

#undef LOCTEXT_NAMESPACE
