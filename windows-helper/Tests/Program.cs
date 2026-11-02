// Tests for the helper's rules, with no Windows and no board. `dotnet run --project Tests`
using System.Text;
using System.Text.Json;
using MediaCtrl.Core;

int failed = 0;
void Check(string name, bool ok)
{
    Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {name}");
    if (!ok) failed++;
}

Track T(string key, bool playing) => new(key, key, "Title", "Artist", 100, 10, playing, 0, 0, new[] { Cmd.Toggle });
JsonElement J(string line) => JsonDocument.Parse(line).RootElement;

// MC-R30
var picker = new SessionPicker();
Check("picks the one playing", picker.Pick(new[] { T("a", false), T("b", true) }, null)?.Key == "b");
Check("keeps the last used when several play", picker.Pick(new[] { T("a", true), T("b", true) }, null)?.Key == "b");
Check("keeps the last used when none play", picker.Pick(new[] { T("a", false), T("b", false) }, "a")?.Key == "b");
var fresh = new SessionPicker();
Check("uses the system's current with no history", fresh.Pick(new[] { T("a", false), T("b", false) }, "b")?.Key == "b");
Check("nothing to pick", new SessionPicker().Pick(Array.Empty<Track>(), null) == null);

// MC-Q11
var quiet = new QuietPorts();
var first = quiet.ToProbe(new[] { "COM4", "COM3" }).ToList();
Check("probes every new port, sorted", first.SequenceEqual(new[] { "COM3", "COM4" }));
quiet.MarkQuiet("COM3");
Check("doesn't reopen a quiet port", quiet.ToProbe(new[] { "COM3", "COM4" }).SequenceEqual(new[] { "COM4" }));
quiet.ToProbe(new[] { "COM4" });
Check("a replugged port is new again", quiet.ToProbe(new[] { "COM3", "COM4" }).SequenceEqual(new[] { "COM3", "COM4" }));

// MC-A21, section 7
var cut = Protocol.Cut(new string('é', 100), 11);
Check("cuts on a character boundary", cut.Length == 5 && Encoding.UTF8.GetByteCount(cut) == 10);
var emoji = Protocol.Cut("ab😀cd", 4);
Check("doesn't split a surrogate pair", emoji == "ab");
var longTrack = new Track("k", new string('a', 90), new string('b', 900), new string('c', 900), 1, 0, true, 0, 0,
                          new[] { Cmd.Play, Cmd.Mute });
var np = Protocol.NowPlaying(longTrack, 50, false);
Check("a long line stays inside the board's limit", Encoding.UTF8.GetByteCount(np) < 1536);
Check("the app fits its field", J(np).GetProperty("app").GetString()!.Length <= 39);
Check("mute is sent as 240", J(np).GetProperty("sup")[1].GetInt32() == 240);
Check("keeps non-ASCII readable", Protocol.NowPlaying(longTrack with { Title = "Ærø" }, 1, false).Contains("Ærø"));

// MC-H2, MC-A28
var host = J(Protocol.Host("OFFICE-PC"));
Check("the host line says windows", host.GetProperty("os").GetString() == "windows");
var idle = J(Protocol.Idle(-1, true));
Check("idle carries an unknown volume", idle.GetProperty("vol").GetInt32() == -1 && idle.GetProperty("muted").GetInt32() == 1);

// Lines from the board
Check("reads a command", Protocol.Parse("{\"t\":\"cmd\",\"c\":240}\r\n") is { Type: "cmd", Value: 240 });
Check("reads a volume", Protocol.Parse("{\"t\":\"vol\",\"v\":62}") is { Type: "vol", Value: 62 });
Check("recognises the board", Protocol.IsBoard(Protocol.Parse("{\"t\":\"hello\",\"dev\":\"media-ctrl\",\"fw\":\"0.2.7\"}")));
Check("leaves another board alone", !Protocol.IsBoard(Protocol.Parse("{\"t\":\"hello\",\"dev\":\"flow-ctrl\"}")));
Check("ignores the firmware's log", Protocol.Parse("#alive up=5 heap=1") == null);
Check("ignores broken JSON", Protocol.Parse("{not json") == null);
Check("ignores a wrong type", Protocol.Parse("{\"t\":\"cmd\",\"c\":\"x\"}") == null);

Check("repeat cycles off, all, one", RepeatCycle.Next(0) == 2 && RepeatCycle.Next(2) == 1 && RepeatCycle.Next(1) == 0);

Console.WriteLine(failed == 0 ? "all passed" : $"{failed} failed");
return failed == 0 ? 0 : 1;
