#include "UEcineRenderGraph.h"

#include "Graph/MovieGraphBlueprintLibrary.h"
#include "Graph/MovieGraphConfig.h"
#include "Graph/MovieGraphNode.h"
#include "Graph/MovieGraphPin.h"
#include "Graph/Nodes/MovieGraphDeferredPassNode.h"
#include "Graph/Nodes/MovieGraphFileOutputNode.h"
#include "Graph/Nodes/MovieGraphGlobalOutputSettingNode.h"
#include "Graph/Nodes/MovieGraphRenderLayerNode.h"
#include "Graph/Nodes/MovieGraphSamplingMethodNode.h"
#include "Graph/Nodes/MovieGraphSetMetadataAttributesNode.h"
#include "Graph/Nodes/MovieGraphWarmUpSettingNode.h"
#include "Materials/MaterialInterface.h"
#include "UEcineBurnInWidget.h"

#define LOCTEXT_NAMESPACE "UEcineRender"

namespace
{
	// Nodes whose headers the engine keeps private, that live in a module of another kind, or whose
	// header brings a third-party library with it (OpenEXR) are found by path.
	const TCHAR* const LayeredExrNodeClassPath = TEXT("/Script/MovieRenderPipelineRenderPasses.MovieGraphImageSequenceOutputNode_MultiLayerEXR");
	const TCHAR* const ProResNodeClassPath = TEXT("/Script/AppleProResMedia.MovieGraphAppleProResNode");
	const TCHAR* const ObjectIdNodeClassPath = TEXT("/Script/MoviePipelineMaskRenderPass.MovieGraphObjectIdNode");
	// The engine's own pass materials, as its render pass nodes name them.
	const TCHAR* const WorldDepthMaterialPath = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_WorldDepth.MovieRenderQueue_WorldDepth");
	const TCHAR* const MotionVectorsMaterialPath = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_MotionVectors.MovieRenderQueue_MotionVectors");
	const TCHAR* const WorldNormalMaterialPath = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_WorldNormal.MovieRenderQueue_WorldNormal");

	/**
	 * Sets a node's property from text and switches the property on, by name. For nodes with
	 * no public header, and for the burn-in properties, which each output node class declares
	 * for itself. False when the node has no such property or the text is not a value for it.
	 */
	bool SetNodeProperty(UMovieGraphNode* Node, const TCHAR* PropertyName, const FString& Value)
	{
		const FProperty* Property = Node->GetClass()->FindPropertyByName(PropertyName);
		const FBoolProperty* Override = CastField<FBoolProperty>(Node->GetClass()->FindPropertyByName(*FString::Printf(TEXT("bOverride_%s"), PropertyName)));
		if (Property == nullptr || Override == nullptr)
		{
			return false;
		}
		Override->SetPropertyValue_InContainer(Node, true);
		return Property->ImportText_InContainer(*Value, Node, Node, PPF_None) != nullptr;
	}

	/** Makes a node of a class named by path, or null when the engine plugin the class is in is not enabled. */
	UMovieGraphNode* CreateNodeByPath(UMovieGraphConfig* Graph, const TCHAR* ClassPath)
	{
		UClass* Class = FindObject<UClass>(nullptr, ClassPath);
		return Class != nullptr ? Graph->CreateNodeByClass(Class) : nullptr;
	}

	/**
	 * Joins a pin of one node to a pin of the next. The engine's AddLabeledEdge says whether it
	 * broke an older connection, not whether it made this one (LESSON-API-009), so the pins are asked.
	 */
	bool ConnectNodes(UMovieGraphConfig* Graph, UMovieGraphNode* From, const FName FromPin, UMovieGraphNode* To, const FName ToPin)
	{
		Graph->AddLabeledEdge(From, FromPin, To, ToPin);
		const UMovieGraphPin* Out = From->GetOutputPin(FromPin);
		const UMovieGraphPin* In = To->GetInputPin(ToPin);
		return Out != nullptr && In != nullptr && Out->GetAllConnectedPins().Contains(In);
	}

	/** Joins nodes into a branch that ends at the graph's output of that name. The Globals branch also starts at the graph's input. */
	bool ConnectBranch(UMovieGraphConfig* Graph, const FName BranchName, TConstArrayView<UMovieGraphNode*> Nodes)
	{
		bool bConnected = true;
		if (BranchName == UMovieGraphNode::GlobalsPinName)
		{
			bConnected &= ConnectNodes(Graph, Graph->GetInputNode(), BranchName, Nodes[0], FName());
		}
		for (int32 Index = 1; Index < Nodes.Num(); ++Index)
		{
			bConnected &= ConnectNodes(Graph, Nodes[Index - 1], FName(), Nodes[Index], FName());
		}
		bConnected &= ConnectNodes(Graph, Nodes.Last(), FName(), Graph->GetOutputNode(), BranchName);
		return bConnected;
	}

	FMoviePipelinePostProcessPass MakeExtraPass(const TCHAR* Name, const FString& MaterialPath, bool bHighPrecision)
	{
		FMoviePipelinePostProcessPass Pass;
		Pass.bEnabled = true;
		Pass.Name = Name;
		Pass.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		Pass.bHighPrecisionOutput = bHighPrecision;
		Pass.bUseLosslessCompression = true;
		return Pass;
	}
}

UMovieGraphConfig* UEcine::RenderGraph::Build(const FUEcineRenderSpec& Spec, UObject* Outer, FText& OutError)
{
	const bool bLayeredFile = Spec.Format == EUEcineRenderFormat::MultilayerExr;
	if (!bLayeredFile && (Spec.bDepth || Spec.bNormals || Spec.bMotionVectors || Spec.bMattes))
	{
		OutError = LOCTEXT("PassesNeedLayers", "Depth, normals, motion vectors and mattes need a file format that holds layers.");
		return nullptr;
	}

	UMovieGraphConfig* Graph = NewObject<UMovieGraphConfig>(Outer);

	// What is true of the whole render.
	UMovieGraphGlobalOutputSettingNode* Output = Graph->ConstructRuntimeNode<UMovieGraphGlobalOutputSettingNode>();
	Output->bOverride_OutputDirectory = true;
	Output->OutputDirectory.Path = Spec.OutputDirectory;
	Output->bOverride_OutputResolution = true;
	Output->OutputResolution = UMovieGraphBlueprintLibrary::NamedResolutionFromSize(Spec.Resolution.X, Spec.Resolution.Y);
	Output->bOverride_HandleFrameCount = true;
	Output->HandleFrameCount = UEcine::Deliver::GetHandleFrames(Spec);

	UMovieGraphSetMetadataAttributesNode* Metadata = Graph->ConstructRuntimeNode<UMovieGraphSetMetadataAttributesNode>();
	for (const FUEcineMetadataEntry& Entry : Spec.Metadata)
	{
		Metadata->MetadataAttributeCollection->MetadataAttributes.Emplace(Entry.Name, Entry.Value);
	}
	if (Spec.bSlate)
	{
		Metadata->MetadataAttributeCollection->MetadataAttributes.Emplace(UEcine::Deliver::SlateMetadataName, TEXT("true"));
	}
	if (Spec.bBurnIn)
	{
		Metadata->MetadataAttributeCollection->MetadataAttributes.Emplace(UEcine::Deliver::BurnInMetadataName, TEXT("true"));
	}

	UMovieGraphSamplingMethodNode* Sampling = Graph->ConstructRuntimeNode<UMovieGraphSamplingMethodNode>();
	Sampling->bOverride_TemporalSampleCount = true;
	Sampling->TemporalSampleCount = FMath::Max(Spec.TemporalSamples, 1);

	UMovieGraphWarmUpSettingNode* WarmUp = Graph->ConstructRuntimeNode<UMovieGraphWarmUpSettingNode>();
	WarmUp->bOverride_NumWarmUpFrames = true;
	WarmUp->NumWarmUpFrames = FMath::Max(Spec.WarmUpFrames, 0);

	// The picture and its passes.
	UMovieGraphRenderLayerNode* Layer = Graph->ConstructRuntimeNode<UMovieGraphRenderLayerNode>();
	UMovieGraphDeferredRenderPassNode* Picture = Graph->ConstructRuntimeNode<UMovieGraphDeferredRenderPassNode>();
	Picture->bOverride_bDisableToneCurve = true;
	Picture->bDisableToneCurve = Spec.bSceneLinear;
	Picture->bOverride_AdditionalPostProcessMaterials = true;
	Picture->AdditionalPostProcessMaterials.Reset();
	if (Spec.bDepth)
	{
		Picture->AdditionalPostProcessMaterials.Add(MakeExtraPass(TEXT("depth"), WorldDepthMaterialPath, true));
	}
	if (Spec.bNormals)
	{
		Picture->AdditionalPostProcessMaterials.Add(MakeExtraPass(TEXT("normal"), WorldNormalMaterialPath, false));
	}
	if (Spec.bMotionVectors)
	{
		Picture->AdditionalPostProcessMaterials.Add(MakeExtraPass(TEXT("motion"), MotionVectorsMaterialPath, false));
	}

	TArray<UMovieGraphNode*> GlobalsBranch = { Output, Metadata, Sampling, WarmUp };
	TArray<UMovieGraphNode*> PictureBranch = { Layer, Picture };

	if (Spec.bMattes)
	{
		UMovieGraphNode* Mattes = CreateNodeByPath(Graph, ObjectIdNodeClassPath);
		if (Mattes == nullptr)
		{
			OutError = LOCTEXT("NoMattePlugin", "Mattes need the engine plugin \"Movie Render Queue Additional Render Passes\", which is not enabled.");
			return nullptr;
		}
		PictureBranch.Add(Mattes);
	}

	// The file. One that holds layers gathers every branch, so it sits with the settings of the whole render.
	UMovieGraphFileOutputNode* File = nullptr;
	if (bLayeredFile)
	{
		File = Cast<UMovieGraphFileOutputNode>(CreateNodeByPath(Graph, LayeredExrNodeClassPath));
		// Packed a row at a time without loss: what compositing programs read fastest.
		if (File == nullptr || !SetNodeProperty(File, TEXT("Compression"), TEXT("ZIPS")) || !SetNodeProperty(File, TEXT("LosslessCompression"), TEXT("ZIPS")))
		{
			OutError = LOCTEXT("NoLayeredExr", "OpenEXR files with layers could not be set up: this version of the engine names them differently.");
			return nullptr;
		}
		GlobalsBranch.Add(File);
	}
	else
	{
		File = Cast<UMovieGraphFileOutputNode>(CreateNodeByPath(Graph, ProResNodeClassPath));
		if (File == nullptr)
		{
			OutError = LOCTEXT("NoProResPlugin", "Apple ProRes files need the engine plugin \"Apple ProRes Media\", which is not enabled.");
			return nullptr;
		}
		PictureBranch.Add(File);
		const TCHAR* Quality = Spec.Format == EUEcineRenderFormat::ProRes4444 ? TEXT("ProRes_4444") : TEXT("ProRes_422LT");
		if (!SetNodeProperty(File, TEXT("Quality"), Quality))
		{
			OutError = LOCTEXT("NoProResQuality", "The Apple ProRes quality could not be set: this version of the engine names it differently.");
			return nullptr;
		}
	}
	File->bOverride_FileNameFormat = true;
	File->FileNameFormat = Spec.FileName;

	if (Spec.bBurnIn || Spec.bSlate)
	{
		const bool bBurnInSet = SetNodeProperty(File, TEXT("bEnableBurnIn"), TEXT("true"))
			&& SetNodeProperty(File, TEXT("BurnInClass"), UUEcineBurnInWidget::StaticClass()->GetPathName())
			&& SetNodeProperty(File, TEXT("bCompositeOntoFinalImage"), TEXT("true"));
		if (!bBurnInSet)
		{
			OutError = LOCTEXT("NoBurnIn", "Burn-ins could not be switched on: this version of the engine names the setting differently.");
			return nullptr;
		}
	}

	Graph->AddOutput(FText::FromString(PictureBranchName));
	if (!ConnectBranch(Graph, UMovieGraphNode::GlobalsPinName, GlobalsBranch) || !ConnectBranch(Graph, PictureBranchName, PictureBranch))
	{
		OutError = LOCTEXT("GraphNotConnected", "The render's nodes could not be joined up: this version of the engine connects them differently.");
		return nullptr;
	}
	return Graph;
}

#undef LOCTEXT_NAMESPACE
