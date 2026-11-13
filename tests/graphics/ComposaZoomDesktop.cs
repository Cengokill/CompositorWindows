// Compile in the pinned Composa.App project and start with
// AppBuilder.Configure<PhotoZoomDesktop>().UsePlatformDetect()
//     .WithInterFont().StartWithClassicDesktopLifetime(args).
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Themes.Fluent;
using Avalonia.Threading;
using Composa.App.Controls;
using Composa.Editing;
using Composa.Model;
using Composa.IO;
using System.Globalization;

internal sealed class PhotoZoomDesktop : Application
{
    public override void Initialize() => Styles.Add(new FluentTheme());

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop) return;
        var photo = ImageFiles.Load(Environment.GetEnvironmentVariable("ZOOM_PHOTO")!)!;
        var document = new Document(photo.Width, photo.Height);
        var layer = Layer.Raster("Photo", photo);
        document.Layers.Add(layer);
        document.SetActive(layer.Id);
        var session = new EditorSession(document);
        for (var i = 0; i < 4; i++) session.DuplicateSelectedLayers();
        var canvas = new CanvasView { Width = 640, Height = 640, Session = session,
            ShowPixelGrid = false, ShowTransformControls = false };
        var window = new Window { Title = "Composa photo zoom", Width = 640, Height = 640,
            CanResize = false, Content = canvas };
        var center = new Point(320, 320);
        var zoom = double.Parse(Environment.GetEnvironmentVariable("ZOOM_START") ?? "0.25", CultureInfo.InvariantCulture);
        window.KeyDown += (_, e) =>
        {
            if (e.Key != Key.F8) return;
            canvas.Fit();
            canvas.ZoomTo(zoom, center);
        };
        window.Opened += (_, _) => Dispatcher.UIThread.Post(() =>
        {
            if (window.RenderScaling != 1) throw new InvalidOperationException("Use display scaling at 100%.");
            canvas.Fit();
            canvas.ZoomTo(zoom, center);
            var origin = canvas.PointToScreen(default);
            Console.WriteLine($"CANVAS {origin.X} {origin.Y} 640 640");
            Console.Out.Flush();
        }, DispatcherPriority.Background);
        desktop.MainWindow = window;
        base.OnFrameworkInitializationCompleted();
    }
}
