// Copy into tests/Composa.App.Tests at Composa 86529d3 and run with
// dotnet test -c Release --filter FullyQualifiedName~ComposaZoomBenchmark
// ZOOM_CASE takes: edge visible hidden notches interval_ms direction trials cold matched.
using System.Diagnostics;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Headless.XUnit;
using Avalonia.Input;
using Avalonia.Threading;
using Composa.App.Controls;
using Composa.Editing;
using Composa.IO;
using Composa.Model;
using Composa.Rendering;
using SkiaSharp;

namespace Composa.App.Tests;

public sealed class ComposaZoomBenchmark(ITestOutputHelper output)
{
    private const int View = 640;

    private static unsafe SKBitmap Noise(int edge, int layer)
    {
        var bitmap = Pixels.NewColor(edge, edge);
        var pixels = (byte*)bitmap.GetPixels();
        for (var y = 0; y < edge; y++)
        for (var x = 0; x < edge; x++)
        {
            var bits = (uint)x * 73856093u ^ (uint)y * 19349663u ^ (uint)(layer + 1) * 83492791u;
            bits = (bits ^ (bits >> 16)) * 0x7feb352du;
            bits = (bits ^ (bits >> 15)) * 0x846ca68bu;
            bits ^= bits >> 16;
            var alpha = layer == 0 ? 255u : 48u + (bits >> 24) % 176u;
            var pixel = pixels + y * bitmap.RowBytes + x * 4;
            for (var channel = 0; channel < 3; channel++)
                pixel[channel] = (byte)((((bits >> (channel * 8)) & 255) * alpha + 127) / 255);
            pixel[3] = (byte)alpha;
        }
        bitmap.NotifyPixelsChanged();
        return bitmap;
    }

    [AvaloniaFact]
    public void WheelToFrame()
    {
        var values = (Environment.GetEnvironmentVariable("ZOOM_CASE") ?? "1024 5 0 5 30 1 20 0 1")
            .Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).ToArray();
        var (edge, visible, hidden) = (values[0], values[1], values[2]);
        var (notches, interval, direction, trials) = (values[3], values[4], values[5], values[6]);
        var (cold, matched) = (values[7] != 0, values[8] != 0);
        var source = Math.Min(edge, (int)Math.Sqrt(500_000_000.0 / (visible + hidden)));
        using var photo = Environment.GetEnvironmentVariable("ZOOM_PHOTO") is { } path
            ? ImageFiles.Load(path) ?? throw new InvalidOperationException("Cannot decode the benchmark photo.")
            : null;
        if (photo != null)
        {
            Assert.Equal((edge, 5, 0), (Math.Max(photo.Width, photo.Height), visible, hidden));
        }
        var document = new Document(photo?.Width ?? edge, photo?.Height ?? edge);
        for (var i = 0; i < visible + hidden; i++)
        {
            document.Layers.Add(new Layer
            {
                Pixels = photo ?? Noise(source, i), Visible = i < visible,
                Transform = LayerTransform.Identity(document.Width, document.Height)
            });
        }
        var canvas = new CanvasView { Width = View, Height = View, Session = new EditorSession(document) };
        var window = new Window { Width = View, Height = View, Content = canvas };
        var anchor = new Point(View / 2, View / 2);
        window.Show();
        void Draw()
        {
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
            using var frame = window.CaptureRenderedFrame();
            Assert.NotNull(frame);
        }
        Draw();
        var factor = matched ? 1.25 : 1.20;
        var wheelDelta = direction * Math.Log(factor) / Math.Log(1.2);
        var low = 512.0 / edge;
        var initialZoom = direction > 0 ? low : Math.Min(low * Math.Pow(factor, notches), 32);
        var first = new List<double>();
        var final = new List<double>();
        var frameIntervals = new List<double>();
        var dispatch = new List<double>();
        try
        {
            for (var trial = -2; trial < trials; trial++)
            {
                canvas.ZoomTo(initialZoom, anchor);
                Draw();
                if (cold)
                    foreach (var layer in document.Layers)
                        Pixels.Invalidate(layer.Pixels!);
                var clock = Stopwatch.StartNew();
                double previous = 0;
                for (var notch = 0; notch < notches; notch++)
                {
                    var due = notch * interval;
                    while (clock.Elapsed.TotalMilliseconds < due)
                        Thread.Sleep(1);
                    if (trial >= 0) dispatch.Add(clock.Elapsed.TotalMilliseconds - due);
                    var before = canvas.Zoom;
                    window.MouseWheel(anchor, new Vector(0, wheelDelta), RawInputModifiers.Alt);
                    Assert.Equal(before * Math.Pow(factor, direction), canvas.Zoom, 8);
                    Draw();
                    var now = clock.Elapsed.TotalMilliseconds;
                    if (trial >= 0)
                    {
                        if (notch == 0) first.Add(now - due);
                        if (notch == notches - 1) final.Add(now - due);
                        frameIntervals.Add(now - previous);
                    }
                    previous = now;
                }
            }
            void Report(string name, List<double> samples)
            {
                samples.Sort();
                double At(double percentile) => samples[(int)Math.Ceiling(samples.Count * percentile) - 1];
                output.WriteLine(string.Create(CultureInfo.InvariantCulture,
                    $"{name} p50={At(.5):F3} p95={At(.95):F3} max={samples[^1]:F3}"));
            }
            output.WriteLine($"Composa Avalonia Headless/Skia case={string.Join(' ', values)}");
            Report("first_notch", first);
            Report("last_notch", final);
            Report("frame_interval", frameIntervals);
            Report("dispatch_delay", dispatch);
            output.WriteLine($"peak_working_set={Process.GetCurrentProcess().PeakWorkingSet64}");
        }
        finally
        {
            window.Close();
        }
    }
}
