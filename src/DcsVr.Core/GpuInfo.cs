using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace DcsVr.Core;

/// <summary>A display adapter Windows has a driver for.</summary>
/// <param name="Vendor">NVIDIA, AMD, Intel or Other (from the PCI vendor id).</param>
public sealed record GpuAdapter(string Name, string Vendor, string? Driver);

public sealed record DesktopFrameGenerationCapability(bool? Supported, int? MaxMultiplier, bool? DynamicSupported,
    string Note, string? DynamicRequirement = null, string? MultiplierRequirement = null);

/// <summary>
/// The PC's graphics adapters, read once from the display class in the registry (no GPU API is loaded). DLSS 5,
/// Foveated Super Resolution and NVIDIA optical-flow frame generation need an NVIDIA RTX card; FidelityFX frame
/// generation runs on any of them. Virtual adapters (remote desktop, TeamViewer, Parsec) are left out.
/// </summary>
public static partial class GpuInfo
{
    private static readonly Lazy<IReadOnlyList<GpuAdapter>> Detected = new(ReadRegistry);
    /// <summary>Replaceable so tests and the offline verification never depend on the build machine's GPU.</summary>
    public static Func<IReadOnlyList<GpuAdapter>> Adapters { get; set; } = () => Detected.Value;

    /// <summary>An NVIDIA GeForce/Quadro/data-center RTX (or TITAN RTX) adapter is present.</summary>
    public static bool HasNvidiaRtx => Adapters().Any(IsNvidiaRtx);
    /// <summary>The adapters could not be read: nothing is blocked on the GPU's account.</summary>
    public static bool Unknown => Adapters().Count == 0;
    public static bool IsNvidiaRtx(GpuAdapter a) => a.Vendor == "NVIDIA" && RtxRegex().IsMatch(a.Name);
    /// <summary>"AMD Radeon RX 7900 XTX" (the names of the real adapters, for messages).</summary>
    public static string Names => string.Join(", ", Adapters().Select(a => a.Name));

    /// <summary>Pre-launch GeForce options from the installed hardware and driver; the backend supplies runtime support.</summary>
    public static DesktopFrameGenerationCapability DesktopFrameGeneration
    {
        get
        {
            var adapters = Adapters();
            var nvidia = adapters.Where(a => a.Vendor == "NVIDIA").ToArray();
            if (adapters.Count == 0 || nvidia.Length > 1)
                return new(null, null, null, "The rendering GPU is awaiting confirmation.");
            if (nvidia.Length == 0)
                return new(false, 1, false, "Native DLSS frame generation requires a GeForce RTX 40 or 50 Series GPU.");
            var card = nvidia[0];
            var series = DesktopRtxRegex().Match(card.Name);
            if (!series.Success)
                return new(null, null, null, $"The rendering capabilities of {card.Name} are awaiting confirmation.");
            if (series.Groups[1].Value is "20" or "30")
                return new(false, 1, false, $"{card.Name} supports DLSS Super Resolution; native frame generation starts with RTX 40 Series.");
            if (series.Groups[1].Value == "40")
                return new(true, 2, false, $"{card.Name} supports 2x native frame generation.");
            var driver = NvidiaDriverNumber(card.Driver);
            // Streamline 2.14.16's DLSS-G guide specifies 595.41 for Dynamic MFG.
            var dynamic = driver is { } version ? version >= 59541 : (bool?)null;
            // NVIDIA's March 31, 2026 release specifies 595.97 for the full 6x feature set.
            var max = driver is >= 59597 ? 6 : 4;
            return new(true, max, dynamic,
                driver is null ? $"{card.Name}: confirm the driver version for 6x and Dynamic MFG."
                    : $"{card.Name} supports up to {max}x with the detected driver.",
                dynamic != true ? "Dynamic MFG requires NVIDIA driver 595.41 or newer." : null,
                max < 6 ? "6x MFG requires NVIDIA driver 595.97 or newer." : null);
        }
    }

    private static int? NvidiaDriverNumber(string? text)
    {
        if (!Version.TryParse(text, out var version)) return null;
        // Windows stores 617.42 as 32.0.16.1742.
        if (version.Build >= 0 && version.Revision >= 0)
            return version.Build % 10 * 10000 + version.Revision;
        return version.Major >= 100 && version.Minor is >= 0 and < 100
            ? version.Major * 100 + version.Minor : null;
    }

    [GeneratedRegex(@"\bRTX\b|\bTITAN RTX\b|\b(?:A|L)\d{2,4}\b|\bH100\b", RegexOptions.IgnoreCase)] private static partial Regex RtxRegex();
    [GeneratedRegex(@"\bGeForce\s+RTX\s+(20|30|40|50)\d{2}(?:D)?\b", RegexOptions.IgnoreCase)] private static partial Regex DesktopRtxRegex();
    [GeneratedRegex(@"VEN_([0-9A-F]{4})", RegexOptions.IgnoreCase)] private static partial Regex VendorRegex();

    private static IReadOnlyList<GpuAdapter> ReadRegistry()
    {
        var adapters = new List<GpuAdapter>();
        try
        {
            using var display = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}");
            foreach (var name in display?.GetSubKeyNames().Where(k => k.Length == 4 && k.All(char.IsDigit)) ?? [])
            {
                using var key = display!.OpenSubKey(name);
                if (key?.GetValue("DriverDesc") is not string description) continue;
                var device = key.GetValue("MatchingDeviceId") as string ?? "";
                var vendor = VendorRegex().Match(device) is { Success: true } m ? m.Groups[1].Value.ToUpperInvariant() switch
                {
                    "10DE" => "NVIDIA", "1002" or "1022" => "AMD", "8086" => "Intel", _ => "Other"
                } : "Other";
                // Only real PCI adapters: virtual displays (remote desktop, TeamViewer, Parsec, IddCx) have no PCI vendor.
                if (vendor == "Other" && !device.StartsWith("pci", StringComparison.OrdinalIgnoreCase)) continue;
                adapters.Add(new(description.Trim(), vendor, key.GetValue("DriverVersion") as string));
            }
        }
        catch (Exception e) when (e is System.Security.SecurityException or UnauthorizedAccessException or IOException) { }
        return adapters;
    }
}
