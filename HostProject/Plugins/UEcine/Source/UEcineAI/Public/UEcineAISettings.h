// Copyright 2026 Fiavaion. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UEcineAISettings.generated.h"

/** Where a picture job is run. */
UENUM()
enum class EUEcineAIBackend : uint8
{
	/** Inside the editor, on this machine's graphics card. */
	Editor UMETA(DisplayName = "In the Editor"),
	/** In a helper program on this machine, started by UEcine with the user's Python. */
	Helper UMETA(DisplayName = "Helper on This Machine"),
	/** On a machine the user or studio runs. Pictures leave this machine, so it asks first. */
	Server UMETA(DisplayName = "Server Elsewhere")
};

/**
 * Per-user preferences for UEcine's AI features, under Editor Preferences > Plugins > UEcine AI.
 * Kept in the user's own settings for this project, never in project files that are shared:
 * they hold keys and what the user has agreed to.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "UEcine AI"))
class UECINEAI_API UUEcineAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Where depth, matte and focal length jobs are run. */
	UPROPERTY(config, EditAnywhere, Category = "Picture Jobs", meta = (DisplayName = "Run Picture Jobs"))
	EUEcineAIBackend Backend = EUEcineAIBackend::Editor;

	/**
	 * The folder model files are kept in, shared by all projects. Left empty, it is UEcine/Models
	 * in this user's local application data.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Picture Jobs", meta = (DisplayName = "Models Folder"))
	FDirectoryPath ModelsFolder;

	/** The Python the helper is started with: a python.exe that has onnxruntime, numpy and pillow. Left empty, it is "python" as the system finds it. */
	UPROPERTY(config, EditAnywhere, Category = "Helper on This Machine", meta = (DisplayName = "Python", FilePathFilter = "exe"))
	FFilePath HelperPython;

	/** The address of a UEcine AI server on another machine, such as https://render01:8750. */
	UPROPERTY(config, EditAnywhere, Category = "Server Elsewhere", meta = (DisplayName = "Address"))
	FString ServerUrl;

	/** The key that server was started with. */
	UPROPERTY(config, EditAnywhere, Category = "Server Elsewhere", meta = (DisplayName = "Key", PasswordField = true))
	FString ServerKey;

	/** Your own Claude API key, for the shot assistant. */
	UPROPERTY(config, EditAnywhere, Category = "Shot Assistant", meta = (DisplayName = "Claude API Key", PasswordField = true))
	FString AssistantKey;

	/** The Claude model the shot assistant asks. */
	UPROPERTY(config, EditAnywhere, Category = "Shot Assistant", meta = (DisplayName = "Model"))
	FString AssistantModel = TEXT("claude-opus-5-5");

	/** Where the shot assistant's requests go. Change it only when your studio routes the Claude API through a gateway of its own. */
	UPROPERTY(config, EditAnywhere, Category = "Shot Assistant", AdvancedDisplay, meta = (DisplayName = "Messages API Address"))
	FString AssistantUrl = TEXT("https://api.anthropic.com/v1/messages");

	/** The hosts the user has agreed to send data to from this project. Changed from the AI tab (see UEcine::AIConsent), so it is not shown here. */
	UPROPERTY(config)
	TArray<FString> ConsentedHosts;

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
};
