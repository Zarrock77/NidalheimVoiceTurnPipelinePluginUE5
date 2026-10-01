using System.IO;
using UnrealBuildTool;

public class NidalheimVoiceTurnPipeline : ModuleRules
{
	public NidalheimVoiceTurnPipeline(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"WebSockets",
			"HTTP", // FGenericPlatformHttp::UrlEncode
			"Sockets",
			"Json",
			"JsonUtilities",
		});

		// miniaudio is vendored as a single header plus a .c translation unit that defines
		// MINIAUDIO_IMPLEMENTATION (see ThirdParty/miniaudio/miniaudio.c). Only WASAPI is enabled,
		// hence the Win64-only platform allow list in the .uplugin.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty", "miniaudio"));
	}
}
