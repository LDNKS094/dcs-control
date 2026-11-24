using System.Text;
using System.Globalization;

namespace DcsVr.Core;

/// <summary>The fixed OptiScaler NR components and each monitor profile's menu settings.</summary>
public static class OptiScalerBackend
{
    public const string IniName = "OptiScaler.ini";
    public const string ChainedName = "dxgi-original.dll";
    public const string ComponentFolder = "components/optiscaler-nr";
    public const string NeuralRuntimePath = "OptiScaler/" + NeuralRuntimeStore.FileName;
    public sealed record Component(string Path, string Sha256);
    public sealed record Manifest(IReadOnlyList<Component> Files, string? SourceCommit = null, string? RuntimeSourceCommit = null);

    public static IReadOnlyList<Component> ReadFiles(string directory)
    {
        var manifest = JsonData.Deserialize<Manifest>(File.ReadAllText(Path.Combine(directory, "manifest.json")));
        // Older development packages listed the user-supplied DLL in the component manifest.
        var files = manifest.Files.Where(file => !file.Path.Equals(NeuralRuntimePath, StringComparison.OrdinalIgnoreCase)).ToArray();
        foreach (var file in files)
        {
            var path = PathPolicy.UnderRoot(directory, file.Path);
            if (!Hashing.FileSha256(path).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Rendering component hash differs from manifest.json: " + file.Path);
        }
        return files;
    }

    public static string SavedPath(string managedRoot, string profileId) =>
        PathPolicy.UnderRoot(managedRoot, "profiles/" + profileId + "/" + IniName);

    // Panel controls apply on launch. Advanced menu keys stay in the profile's saved INI.
    public static IReadOnlyDictionary<string, string> ConfigurationValues(VrProfile profile)
    {
        var settings = profile.OptiScaler;
        var ratio = !settings.SuperResolution ? 1 : settings.Quality switch
        {
            "Quality" => 1.5,
            "Balanced" => 1.7,
            "Performance" => 2,
            "UltraPerformance" => 3,
            _ => throw new InvalidDataException("Select a super resolution quality preset.")
        };
        var values = new Dictionary<string, string>
        {
            ["Upscalers.Dx11Upscaler"] = "dlss_12",
            ["DCSControl.ProfileId"] = profile.Id,
            ["DlssNr.Enabled"] = settings.NeuralRendering ? "true" : "false",
            ["DlssNr.WorkingScale"] = settings.WorkingScale.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.RunBeforeSR"] = settings.NeuralBeforeUpscaling ? "true" : "false",
            // These advanced placement modes override RunBeforeSR; the panel owns the processing position.
            ["DlssNr.FinishedPicture"] = "false",
            ["DlssNr.DeferredDLSS"] = "false",
            ["DlssNr.Intensity"] = settings.Intensity.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.LocalTone"] = settings.LocalToneStrength.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.LocalStructure"] = settings.LocalStructureStrength.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.Style"] = (settings.ModelStyle switch { "Standard" => 0, "Natural" => 1, "Cinematic" => 2,
                _ => throw new InvalidDataException("Select an NR model style.") }).ToString(CultureInfo.InvariantCulture),
            ["DlssNr.AutoMask"] = settings.AutomaticMask ? "true" : "false",
            ["DlssNr.SkinStructure"] = settings.SkinStructureStrength.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.ColourStrength"] = settings.ColourStrength.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.TransferStrength"] = settings.TransferStrength.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.WhitePointScale"] = settings.PaperWhiteScale.ToString(CultureInfo.InvariantCulture),
            ["InitFlags.DepthInverted"] = settings.DepthConvention switch
            {
                NeuralDepthMode.Game => "auto",
                NeuralDepthMode.Normal => "false",
                NeuralDepthMode.Reversed => "true",
                _ => throw new InvalidDataException("Select a depth convention.")
            },
            ["DLSS.RenderPresetOverride"] = settings.Preset == 0 ? "false" : "true",
            ["DLSS.RenderPresetForAll"] = settings.Preset.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.SpatialCompression"] = settings.PeripheralCompression ? "true" : "false",
            ["DlssNr.SpatialCenterX"] = settings.CenterWidth.ToString(CultureInfo.InvariantCulture),
            ["DlssNr.SpatialCenterY"] = settings.CenterHeight.ToString(CultureInfo.InvariantCulture),
            // Keep the center at full density and retain this share of the remaining periphery. A 100% center stays full size.
            ["DlssNr.SpatialWorkX"] = (settings.CenterWidth + (100 - settings.CenterWidth) * settings.PeripheralRetention / 100).ToString(CultureInfo.InvariantCulture),
            ["DlssNr.SpatialWorkY"] = (settings.CenterHeight + (100 - settings.CenterHeight) * settings.PeripheralRetention / 100).ToString(CultureInfo.InvariantCulture),
            ["DlssNr.SpatialOffsetX"] = "0",
            ["DlssNr.SpatialOffsetY"] = "0",
            ["DlssNr.SpatialShiftX"] = "0",
            ["DlssNr.SpatialShiftY"] = "0",
            ["UpscaleRatio.UpscaleRatioOverrideEnabled"] = "true",
            ["UpscaleRatio.UpscaleRatioOverrideValue"] = ratio.ToString(CultureInfo.InvariantCulture),
            ["FrameGen.Enabled"] = settings.FrameGeneration ? "true" : "false",
            ["FrameGen.FGInput"] = "upscaler",
            ["FrameGen.FGOutput"] = "dlssg",
            ["FrameGen.FGNvngxReplacement"] = "None",
            ["DLSSG.InterpolationCount"] = (settings.FrameGenerationFactor - 1).ToString(CultureInfo.InvariantCulture),
            ["DLSSG.ForceDMFG"] = settings.DynamicFrameGeneration ? "true" : "false",
            // Auto chooses its multiplier; the output cap is controlled separately through the Reflex frame limiter.
            ["DLSSG.FramerateTargetDMFG"] = "0",
            ["V-Sync.OverrideVsync"] = settings.VSync.HasValue ? "true" : "false",
            ["V-Sync.ForceVsync"] = settings.VSync is { } vsync ? (vsync ? "true" : "false") : "auto",
            ["V-Sync.SyncInterval"] = settings.VSync == true ? "1" : "0",
            ["Menu.ShowFps"] = settings.ShowFps ? "true" : "false",
            ["Menu.FpsOverlayType"] = "1",
            ["Log.LogToFile"] = settings.LogToFile ? "true" : "false"
        };
        if (NeuralHotkeys.TryParse(profile.DesktopMenuKey, out var menuKey) && NeuralHotkeys.IsMenuShortcut(menuKey, desktop: true))
        {
            values["Menu.ShortcutKey"] = menuKey.VirtualKey.ToString(CultureInfo.InvariantCulture);
            values["Menu.ShortcutKeyRequireCtrl"] = (menuKey.Modifiers & 1) != 0 ? "true" : "false";
            values["Menu.ShortcutKeyRequireAlt"] = (menuKey.Modifiers & 2) != 0 ? "true" : "false";
            values["Menu.OverlayMenu"] = "true";
        }
        if (settings.OutputFrameLimit != FpsLimitMode.Preserve)
            values["Framerate.FramerateLimit"] = settings.OutputFrameLimit switch
            {
                FpsLimitMode.MatchRefresh => new WindowsDisplayModes().Current(settings.OutputDisplay.Length > 0 ? settings.OutputDisplay : new WindowsDisplayModes().PrimaryDevice()!)!.Refresh.ToString(CultureInfo.InvariantCulture),
                FpsLimitMode.Custom => settings.FrameRateTarget.ToString(CultureInfo.InvariantCulture),
                _ => throw new InvalidDataException("Select an output frame limit.")
            };
        return values;
    }

    public static byte[] Configuration(string template, string? saved, VrProfile profile) =>
        new UTF8Encoding(false).GetBytes(IniFile.SetValues(saved ?? template, ConfigurationValues(profile)));
}
