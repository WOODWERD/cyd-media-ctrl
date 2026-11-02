// Media Ctrl helper for Windows: the rules that don't touch Windows, so they test anywhere.
// WOODWERD LLC, MIT licence. The USB message format is documented in src/host_link.h.
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MediaCtrl.Core;

/// Command numbers used by the firmware (Media::Cmd, the same as Apple Media Service RemoteCommandID).
public static class Cmd
{
    public const int Play = 0, Pause = 1, Toggle = 2, Next = 3, Prev = 4, VolUp = 5, VolDown = 6,
                     Repeat = 7, Shuffle = 8, SkipFwd = 9, SkipBack = 10, Like = 11, Mute = 0xF0;
}

public record Track(string Key, string App, string Title, string Artist, double Dur, double Pos,
                    bool Playing, int Shuffle, int Repeat, IReadOnlyList<int> Sup);

public record Incoming(string Type, int Value, string Dev);

public static class Protocol
{
    // The board's string fields are 40, 160 and 120 bytes, and a line is limited to 1536 (MC-R, section 7).
    const int AppBytes = 39, TitleBytes = 159, ArtistBytes = 119;

    public const string Who = "{\"t\":\"who\"}\n";

    /// Cuts on a character boundary, so the board never gets half a UTF-8 sequence.
    public static string Cut(string s, int maxBytes)
    {
        if (Encoding.UTF8.GetByteCount(s) <= maxBytes) return s;
        var sb = new StringBuilder();
        int used = 0;
        foreach (var rune in s.EnumerateRunes())
        {
            if (used + rune.Utf8SequenceLength > maxBytes) break;
            sb.Append(rune.ToString());
            used += rune.Utf8SequenceLength;
        }
        return sb.ToString();
    }

    static readonly JsonSerializerOptions Options = new()
    {
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        TypeInfoResolver = new System.Text.Json.Serialization.Metadata.DefaultJsonTypeInfoResolver(),
    };

    static string Line(JsonObject o) => o.ToJsonString(Options) + "\n";

    public static string Host(string name) =>
        Line(new JsonObject { ["t"] = "host", ["name"] = Cut(name, ArtistBytes), ["os"] = "windows" });

    public static string Idle(int vol, bool muted) =>
        Line(new JsonObject { ["t"] = "idle", ["vol"] = vol, ["muted"] = muted ? 1 : 0 });

    public static string NowPlaying(Track t, int vol, bool muted)
    {
        var sup = new JsonArray();
        foreach (var c in t.Sup) sup.Add(c);
        return Line(new JsonObject
        {
            ["t"] = "np",
            ["app"] = Cut(t.App, AppBytes),
            ["title"] = Cut(t.Title, TitleBytes),
            ["artist"] = Cut(t.Artist, ArtistBytes),
            ["dur"] = Math.Round(t.Dur, 1),
            ["pos"] = Math.Round(t.Pos, 1),
            ["state"] = t.Playing ? 1 : 0,
            ["vol"] = vol,
            ["muted"] = muted ? 1 : 0,
            ["shuffle"] = t.Shuffle,
            ["repeat"] = t.Repeat,
            ["sup"] = sup,
        });
    }

    /// Reads one line from the board. Its own log lines, and anything else that isn't ours, give null.
    public static Incoming? Parse(string line)
    {
        line = line.Trim();
        if (!line.StartsWith('{')) return null;
        try
        {
            var o = JsonNode.Parse(line)?.AsObject();
            if (o == null) return null;
            var type = o["t"]?.GetValue<string>() ?? "";
            return type switch
            {
                "cmd" => new Incoming(type, o["c"]?.GetValue<int>() ?? -1, ""),
                "vol" => new Incoming(type, o["v"]?.GetValue<int>() ?? 0, ""),
                "hello" => new Incoming(type, 0, o["dev"]?.GetValue<string>() ?? ""),
                _ => null,
            };
        }
        catch (Exception e) when (e is JsonException or InvalidOperationException or FormatException)
        {
            return null;
        }
    }

    public static bool IsBoard(Incoming? m) => m is { Type: "hello", Dev: "media-ctrl" };
}

/// Chooses which media session is "now playing" (MC-R30).
public class SessionPicker
{
    string? last;

    public Track? Pick(IReadOnlyList<Track> sessions, string? systemCurrent)
    {
        var playing = sessions.Where(s => s.Playing).ToList();
        var pick = playing.FirstOrDefault(s => s.Key == last) ?? playing.FirstOrDefault()
                   ?? sessions.FirstOrDefault(s => s.Key == last)
                   ?? sessions.FirstOrDefault(s => s.Key == systemCurrent)
                   ?? sessions.FirstOrDefault();
        if (pick != null) last = pick.Key;
        return pick;
    }
}

/// Opening a port restarts whatever board is on it, so a port that didn't answer is asked only once.
/// Unplugged and back counts as new (MC-Q11).
public class QuietPorts
{
    readonly HashSet<string> quiet = new();

    public IEnumerable<string> ToProbe(IEnumerable<string> present)
    {
        var ports = present.Distinct().OrderBy(p => p).ToList();
        quiet.IntersectWith(ports);
        return ports.Where(p => !quiet.Contains(p)).ToList();
    }

    public void MarkQuiet(string port) => quiet.Add(port);
}

/// Repeat follows the Mac helper's Music order: off, all, one.
public static class RepeatCycle
{
    public static int Next(int repeat) => repeat switch { 0 => 2, 2 => 1, _ => 0 };
}
