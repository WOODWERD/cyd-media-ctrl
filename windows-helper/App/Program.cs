// Media Ctrl helper for Windows, WOODWERD LLC, MIT licence.
// Feeds the board what Windows says is playing, over its USB cable, and carries its presses back.
// It has no window. It reports through the log at %LOCALAPPDATA%\MediaCtrl\MediaCtrl.log.
//   MediaCtrlHelper.exe            run until stopped
//   MediaCtrlHelper.exe --once     write what it would send to the log, then exit
//   MediaCtrlHelper.exe --port COM5   use this port and skip the search
using System.IO.Ports;
using System.Text;
using MediaCtrl.Core;

namespace MediaCtrl;

static class Program
{
    const string Version = "0.2.7";
    const int PollMs = 1000, KeepAliveMs = 4000, AskMs = 1500, ProbeMs = 4000;
    static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MediaCtrl", "MediaCtrl.log");
    static readonly object LogLock = new();

    static void Log(string s)
    {
        lock (LogLock)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            if (File.Exists(LogPath) && new FileInfo(LogPath).Length > 1_000_000) File.Delete(LogPath);
            File.AppendAllText(LogPath, $"{DateTime.Now:HH:mm:ss} {s}{Environment.NewLine}");
        }
    }

    static async Task<int> Main(string[] args)
    {
        var portArg = Array.IndexOf(args, "--port") is var i and >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        using var one = new Mutex(true, "WOODWERD.MediaCtrl.Helper", out var first);
        if (!first) { Log("already running"); return 0; }

        var media = await WindowsMedia.Create();
        if (args.Contains("--once")) { Log(await media.Read()); return 0; }

        Log($"Media Ctrl helper {Version}");
        var quiet = new QuietPorts();
        while (true)
        {
            var port = FindBoard(portArg, quiet);
            if (port == null) { await Task.Delay(3000); continue; }
            await RunLink(port, media);
            try { port.Close(); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
            Log("board gone; looking again");   // MC-S10
            await Task.Delay(2000);
        }
    }

    static SerialPort Open(string name)
    {
        var p = new SerialPort(name, 115200) { ReadTimeout = 200, WriteTimeout = 1000, Encoding = Encoding.UTF8, NewLine = "\n" };
        p.DtrEnable = false;   // held low: the CYD's auto-reset circuit
        p.RtsEnable = false;
        p.Open();
        return p;
    }

    /// MC-R28, MC-S7, MC-S9.
    static SerialPort? FindBoard(string? explicitPort, QuietPorts quiet)
    {
        var ports = explicitPort != null ? new[] { explicitPort } : quiet.ToProbe(SerialPort.GetPortNames());
        foreach (var name in ports)
        {
            SerialPort port;
            try { port = Open(name); }
            catch (Exception e) when (e is UnauthorizedAccessException or IOException)
            {
                Log($"port busy: {name}. Close ESP Ctrl if it has it.");
                continue;
            }
            var end = Environment.TickCount64 + ProbeMs;
            var asked = 0L;
            while (Environment.TickCount64 < end)
            {
                try
                {
                    if (Environment.TickCount64 - asked > AskMs) { port.Write(Protocol.Who); asked = Environment.TickCount64; }
                    if (Protocol.IsBoard(Protocol.Parse(port.ReadLine())))
                    {
                        Log($"found Media Ctrl on {name}");
                        return port;
                    }
                }
                catch (TimeoutException) { }
                catch (IOException) { break; }
            }
            port.Close();
            if (explicitPort == null) quiet.MarkQuiet(name);
        }
        return null;
    }

    static async Task RunLink(SerialPort port, WindowsMedia media)
    {
        var alive = true;
        var wake = new SemaphoreSlim(0);
        var work = System.Threading.Channels.Channel.CreateUnbounded<Incoming>();

        var reader = new Thread(() =>
        {
            while (alive)
            {
                try
                {
                    var m = Protocol.Parse(port.ReadLine());
                    if (m != null) work.Writer.TryWrite(m);
                }
                catch (TimeoutException) { }
                catch (Exception e) when (e is IOException or InvalidOperationException or UnauthorizedAccessException)
                {
                    alive = false;
                    wake.Release();
                }
            }
        }) { IsBackground = true };
        reader.Start();

        string? last = null;
        var lastSent = 0L;
        var force = true;
        try
        {
            port.Write(Protocol.Host(Environment.MachineName));
            while (alive)
            {
                while (work.Reader.TryRead(out var m))
                {
                    switch (m.Type)
                    {
                        case "cmd":
                            Log($"command {m.Value}: {(await media.Run(m.Value) ? "ok" : "failed")}");
                            break;
                        case "vol":
                            SystemVolume.Set(m.Value);
                            Log($"volume {m.Value}");
                            break;
                    }
                    force = true;   // report the new state right away, and everything after a board restart
                }
                var line = await media.Read();
                var now = Environment.TickCount64;
                if (force || line != last || now - lastSent > KeepAliveMs)
                {
                    port.Write(line);
                    last = line; lastSent = now; force = false;
                }
                await Task.WhenAny(work.Reader.WaitToReadAsync().AsTask(), wake.WaitAsync(), Task.Delay(PollMs));
            }
        }
        catch (Exception e) when (e is IOException or InvalidOperationException or UnauthorizedAccessException or TimeoutException)
        {
            Log($"link lost: {e.Message}");
        }
        alive = false;
    }
}
