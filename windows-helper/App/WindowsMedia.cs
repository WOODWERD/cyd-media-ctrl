// Reads and drives whatever shows in the Windows media overlay, plus the default output's volume.
using System.Runtime.InteropServices;
using MediaCtrl.Core;
using Windows.Media;
using Windows.Media.Control;
using Session = Windows.Media.Control.GlobalSystemMediaTransportControlsSession;
using Manager = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager;
using Status = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus;

namespace MediaCtrl;

public class WindowsMedia
{
    const int SkipSeconds = 15, VolStep = 6;
    const byte VkPlayPause = 0xB3;
    readonly Manager manager;
    readonly SessionPicker picker = new();
    Session? current;

    WindowsMedia(Manager m) => manager = m;

    public static async Task<WindowsMedia> Create() => new(await Manager.RequestAsync());

    /// The next line for the board (MC-R30 to MC-R36).
    public async Task<string> Read()
    {
        var sessions = manager.GetSessions();
        var tracks = new List<Track>();
        var byKey = new Dictionary<string, Session>();
        foreach (var s in sessions)
        {
            var t = await ReadSession(s);
            if (t == null) continue;
            tracks.Add(t);
            byKey[t.Key] = s;
        }
        var pick = picker.Pick(tracks, manager.GetCurrentSession()?.SourceAppUserModelId);
        var (vol, muted) = SystemVolume.Read();
        if (pick == null) { current = null; return Protocol.Idle(vol, muted); }
        current = byKey[pick.Key];
        return Protocol.NowPlaying(pick, vol, muted);
    }

    static async Task<Track?> ReadSession(Session s)
    {
        try
        {
            var props = await s.TryGetMediaPropertiesAsync();
            var info = s.GetPlaybackInfo();
            var line = s.GetTimelineProperties();
            var controls = info.Controls;
            var playing = info.PlaybackStatus == Status.Playing;

            var dur = line.EndTime.TotalSeconds;
            var pos = line.Position.TotalSeconds;
            if (playing && line.LastUpdatedTime.Year > 2000)   // the app reports position only now and then
                pos += (DateTimeOffset.Now - line.LastUpdatedTime).TotalSeconds;
            if (dur > 0) pos = Math.Min(pos, dur); else dur = 0;

            var sup = new List<int> { Cmd.VolUp, Cmd.VolDown, Cmd.Mute };
            if (controls.IsPlayEnabled) sup.Add(Cmd.Play);
            if (controls.IsPauseEnabled) sup.Add(Cmd.Pause);
            if (controls.IsPlayEnabled || controls.IsPauseEnabled || controls.IsPlayPauseToggleEnabled) sup.Add(Cmd.Toggle);
            if (controls.IsNextEnabled) sup.Add(Cmd.Next);
            if (controls.IsPreviousEnabled) sup.Add(Cmd.Prev);
            if (controls.IsShuffleEnabled) sup.Add(Cmd.Shuffle);
            if (controls.IsRepeatEnabled) sup.Add(Cmd.Repeat);
            if (controls.IsPlaybackPositionEnabled && dur > 0) { sup.Add(Cmd.SkipFwd); sup.Add(Cmd.SkipBack); }

            var repeat = info.AutoRepeatMode switch
            {
                MediaPlaybackAutoRepeatMode.Track => 1,
                MediaPlaybackAutoRepeatMode.List => 2,
                _ => 0,
            };
            return new Track(s.SourceAppUserModelId, AppName(s.SourceAppUserModelId), props?.Title ?? "", props?.Artist ?? "",
                             dur, pos, playing, info.IsShuffleActive == true ? 2 : 0, repeat, sup);
        }
        catch (Exception e) when (e is COMException or InvalidOperationException)
        {
            return null;   // the app closed while it was being read
        }
    }

    /// "Spotify.exe", "MSEdge", "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic" become a short name.
    public static string AppName(string id)
    {
        var known = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["msedge"] = "Edge", ["chrome"] = "Chrome", ["firefox"] = "Firefox", ["spotify"] = "Spotify",
            ["zunemusic"] = "Media Player", ["applemusic"] = "Apple Music", ["vlc"] = "VLC",
        };
        var name = id.Split('!').Last();
        if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) name = name[..^4];
        name = name.Split('.').Last();
        foreach (var (key, label) in known)
            if (id.Contains(key, StringComparison.OrdinalIgnoreCase)) return label;
        return name;
    }

    public async Task<bool> Run(int c)
    {
        switch (c)
        {
            case Cmd.VolUp: SystemVolume.Change(VolStep); return true;
            case Cmd.VolDown: SystemVolume.Change(-VolStep); return true;
            case Cmd.Mute: SystemVolume.ToggleMute(); return true;
        }
        var s = current;
        if (s == null)
        {
            if (c != Cmd.Play && c != Cmd.Toggle) return false;
            SystemVolume.PressKey(VkPlayPause);   // nothing to talk to: the media key resumes the last app (MC-R37)
            return true;
        }
        var info = s.GetPlaybackInfo();
        switch (c)
        {
            case Cmd.Play: return await s.TryPlayAsync();
            case Cmd.Pause: return await s.TryPauseAsync();
            case Cmd.Toggle: return await s.TryTogglePlayPauseAsync();
            case Cmd.Next: return await s.TrySkipNextAsync();
            case Cmd.Prev: return await s.TrySkipPreviousAsync();
            case Cmd.Shuffle: return await s.TryChangeShuffleActiveAsync(info.IsShuffleActive != true);
            case Cmd.Repeat:
                var now = info.AutoRepeatMode switch
                {
                    MediaPlaybackAutoRepeatMode.Track => 1,
                    MediaPlaybackAutoRepeatMode.List => 2,
                    _ => 0,
                };
                return await s.TryChangeAutoRepeatModeAsync(RepeatCycle.Next(now) switch
                {
                    1 => MediaPlaybackAutoRepeatMode.Track,
                    2 => MediaPlaybackAutoRepeatMode.List,
                    _ => MediaPlaybackAutoRepeatMode.None,
                });
            case Cmd.SkipFwd:
            case Cmd.SkipBack:
                var line = s.GetTimelineProperties();
                var delta = TimeSpan.FromSeconds(c == Cmd.SkipFwd ? SkipSeconds : -SkipSeconds);
                var target = line.Position + delta;
                if (target < TimeSpan.Zero) target = TimeSpan.Zero;
                return await s.TryChangePlaybackPositionAsync(target.Ticks);
            default: return false;   // Like has no Windows command (MC-R33)
        }
    }
}

/// The default output device, through Core Audio.
public static class SystemVolume
{
    /// (0 to 100, or -1 if it can't be read, and muted). MC-R35.
    public static (int, bool) Read()
    {
        try
        {
            var v = Endpoint();
            Marshal.ThrowExceptionForHR(v.GetMasterVolumeLevelScalar(out var level));
            Marshal.ThrowExceptionForHR(v.GetMute(out var muted));
            return ((int)Math.Round(level * 100), muted);
        }
        catch (Exception e) when (e is COMException or InvalidCastException) { return (-1, false); }
    }

    public static void Set(int level)
    {
        var scalar = Math.Clamp(level, 0, 100) / 100f;
        var empty = Guid.Empty;
        Marshal.ThrowExceptionForHR(Endpoint().SetMasterVolumeLevelScalar(scalar, ref empty));
    }

    public static void Change(int by)
    {
        var (vol, _) = Read();
        if (vol >= 0) Set(vol + by);
    }

    public static void ToggleMute()
    {
        var v = Endpoint();
        Marshal.ThrowExceptionForHR(v.GetMute(out var muted));
        var empty = Guid.Empty;
        Marshal.ThrowExceptionForHR(v.SetMute(!muted, ref empty));
    }

    public static void PressKey(byte key)
    {
        keybd_event(key, 0, 0, UIntPtr.Zero);
        keybd_event(key, 0, 2, UIntPtr.Zero);   // key up
    }

    static IAudioEndpointVolume Endpoint()
    {
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
        Marshal.ThrowExceptionForHR(enumerator.GetDefaultAudioEndpoint(0 /* render */, 1 /* multimedia */, out var device));
        var iid = typeof(IAudioEndpointVolume).GUID;
        Marshal.ThrowExceptionForHR(device.Activate(ref iid, 1 /* in-process */, IntPtr.Zero, out var volume));
        return (IAudioEndpointVolume)volume;
    }

    [DllImport("user32.dll")]
    static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumerator { }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int flow, int mask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    }

    [ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioEndpointVolume
    {
        [PreserveSig] int RegisterControlChangeNotify(IntPtr notify);
        [PreserveSig] int UnregisterControlChangeNotify(IntPtr notify);
        [PreserveSig] int GetChannelCount(out uint count);
        [PreserveSig] int SetMasterVolumeLevel(float db, ref Guid context);
        [PreserveSig] int SetMasterVolumeLevelScalar(float level, ref Guid context);
        [PreserveSig] int GetMasterVolumeLevel(out float db);
        [PreserveSig] int GetMasterVolumeLevelScalar(out float level);
        [PreserveSig] int SetChannelVolumeLevel(uint channel, float db, ref Guid context);
        [PreserveSig] int SetChannelVolumeLevelScalar(uint channel, float level, ref Guid context);
        [PreserveSig] int GetChannelVolumeLevel(uint channel, out float db);
        [PreserveSig] int GetChannelVolumeLevelScalar(uint channel, out float level);
        [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid context);
        [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
    }
}
